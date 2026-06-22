#pragma once
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "config.hpp"
#include "miniz.h"
#include "threadPool.hpp"

namespace fs = std::filesystem;

/*
    Controlla se il path passato è una directory
*/
inline bool isDirectory(const char* path) {
    std::error_code ec;
    const fs::path p(path);
    const auto st = fs::symlink_status(p, ec);  // controlla se è un link simbolico
    if (ec) return false;                       // ec=true se è un link simbolico associato ad una directory

    if (fs::is_directory(st)) return true;  // controlla se è una directory
    return false;
}
inline size_t get_file_size(const char* path) {
    const fs::path p(path);
    try {
        return fs::file_size(p);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Error: cannot get file size for %s (%s)\n", path, e.what());
        return 0;
    }
}
/*
    Controlla se è un file con cui dobbiamo effettivamente interagire o se è "saltabile"
*/
inline bool
should_process(const std::filesystem::path& p) {
    const std::string ext = p.extension().string();  // estrae l'estensione
    if (cfg.mode == COMP) {
        // se è un file tomperaneo o già compresso lo salta
        if (ext == cfg.out_ext || ext == ".tmp")
            return false;
        return true;
    } else {
        return (ext == cfg.out_ext);  // decomprime solamente i file .defl
    }
}

namespace {
constexpr std::size_t CHUNK = 256 * 1024;

/**
 * Compress an input byte stream to an output byte stream using zlib/DEFLATE.
 *
 * Reads in fixed-size chunks (CHUNK), feeds them to zlib's deflate(), and writes
 * the produced compressed bytes to out.
 *
 * Returns:
 *  - true  on success (no zlib or I/O errors encountered).
 *  - false if deflate() signals an error (e.g., Z_STREAM_ERROR) or if writing to out fails.
 *
 * Notes:
 *  - This is a streaming compressor: it does not need the input size in advance and uses
 *    bounded memory (two CHUNK-sized buffers)
 *  - deflateEnd() is always called to release zlib resources before returning
 *  - On the last iteration, flush = Z_FINISH ensures the compressed stream is properly
 *    terminated; the code breaks out when Z_STREAM_END is reached
 */
inline bool compress_stream(std::istream& in, std::ostream& out, int level) {
    z_stream s{};  // struttura dati per mantenere lo stato e i contatori della compressione

    if (deflateInit(&s, level) != Z_OK) return false;  // allocazione e inizializzazione della memoria interna di Miniz

    std::vector<unsigned char> inbuf(CHUNK), outbuf(CHUNK);  // vettori di buffer temporanei per i dati grezzi (input) e compressi (output)
    bool ok = true;
    int zret = Z_OK;

    while (in) {
        // Leggiamo un chunk di dati (fino a 256KB) dallo stream di input
        in.read(reinterpret_cast<char*>(inbuf.data()), static_cast<std::streamsize>(inbuf.size()));
        s.avail_in = static_cast<uInt>(in.gcount());  // quanti byte effettivi sono stati letti
        s.next_in = inbuf.data();

        // Se il file è finito ordiniamo a Miniz di completare l'archivio (Z_FINISH), altrimenti proseguiamo normalmente (Z_NO_FLUSH)
        const int flush = in.eof() ? Z_FINISH : Z_NO_FLUSH;

        do {
            // Configura il buffer di output prima di ogni chiamata a deflate
            s.avail_out = static_cast<uInt>(outbuf.size());
            s.next_out = outbuf.data();

            // Esegue l'algoritmo di compressione DEFLATE
            zret = deflate(&s, flush);
            if (zret == Z_STREAM_ERROR) {
                ok = false;
                break;
            }

            // Calcola quanti byte compressi sono stati generati in questo passaggio e li scrive sul file di output
            const std::size_t have = outbuf.size() - s.avail_out;
            if (have) out.write(reinterpret_cast<const char*>(outbuf.data()), static_cast<std::streamsize>(have));
            if (!out) {
                // Gestione errore in caso di fallimento della scrittura su disco (es. disco pieno)
                ok = false;
                break;
            }
            // Il ciclo continua se avail_out è 0, segno che il buffer è stato riempito del tutto e Miniz ha ancora dati in attesa
        } while (s.avail_out == 0);

        // Interrompe il ciclo esterno se si è verificato un errore
        if (!ok) break;
        // Se abbiamo raggiunto la fine del file di input e Miniz ha completato con successo l'output, usciamo
        if (flush == Z_FINISH && zret == Z_STREAM_END) break;
    }
    deflateEnd(&s);  // Dealloca la memoria interna di Miniz per evitare memory leak
    return ok;
}

/**
 * Decompress a zlib stream from in to out using a bounded-buffer loop.
 *
 * Return value:
 *  - true  if the stream ends cleanly with Z_STREAM_END and there are no I/O errors.
 *  - false on any zlib error (NEED_DICT/DATA_ERROR/MEM_ERROR/STREAM_ERROR),
 *    or if EOF is reached with no more progress possible (truncated stream),
 *    or on output I/O failure.
 *
 * Notes:
 *  - This expects a zlib stream (inflateInit). If the compressed data was produced
 *    as gzip or raw-deflate, the way to initialize the stream is different (not supported).
 *  - CHUNK controls the I/O granularity; memory usage is ~2*CHUNK + zlib state.
 *  - inflateEnd() is called exactly once on every exit path.
 */
inline bool decompress_stream(std::istream& in, std::ostream& out) {
    constexpr std::size_t CHUNK = 256 * 1024;
    std::vector<unsigned char> inbuf(CHUNK), outbuf(CHUNK);  // Buffer per dati compressi (input) e dati espansi (output)

    z_stream s{};  // Struttura dati per mantenere lo stato e i contatori della decompressione
    if (inflateInit(&s) != Z_OK)
        return false;  // Inizializzazione della memoria interna per l'algoritmo di decompressione

    int zret = Z_OK;
    bool ok = true;

    for (;;) {
        // Lettura pigra (lazy): carichiamo nuovi dati dal file solo quando il buffer di input si svuota completamente
        if (s.avail_in == 0) {
            in.read(reinterpret_cast<char*>(inbuf.data()), static_cast<std::streamsize>(inbuf.size()));
            s.avail_in = static_cast<uInt>(in.gcount());  // Numero di byte compressi effettivamente letti dal disco
            s.next_in = inbuf.data();                     // Puntatore all'inizio dei dati da decomprimere
        }

        // Configurazione del buffer di output per accogliere i dati estratti
        s.avail_out = static_cast<uInt>(outbuf.size());
        s.next_out = outbuf.data();

        // Esecuzione dell'algoritmo di decompressione (Z_NO_FLUSH poiché la fine è determinata dai metadati del flusso)
        zret = inflate(&s, Z_NO_FLUSH);

        // Caso Successo: Miniz ha rilevato la fine naturale del flusso compresso
        if (zret == Z_STREAM_END) {
            const std::size_t have = outbuf.size() - s.avail_out;  // Calcola gli ultimi byte estratti
            if (have) out.write(reinterpret_cast<const char*>(outbuf.data()), static_cast<std::streamsize>(have));
            ok = static_cast<bool>(out);
            inflateEnd(&s);  // Deallocazione della memoria per evitare memory leak
            return ok;
        }

        // Caso Errore: Rilevamento di corruzione dati, errori di memoria o di consistenza dello stream
        if (zret == Z_NEED_DICT || zret == Z_DATA_ERROR || zret == Z_MEM_ERROR || zret == Z_STREAM_ERROR) {
            inflateEnd(&s);
            return false;
        }

        // Scrittura intermedia: se sono stati generati dei byte estratti, li salviamo sul disco
        const std::size_t have = outbuf.size() - s.avail_out;
        if (have) {
            out.write(reinterpret_cast<const char*>(outbuf.data()), static_cast<std::streamsize>(have));
            if (!out) {
                inflateEnd(&s);
                return false;
            }  // Interruzione in caso di fallimento della scrittura
        }

        // Controllo anti-stallo: il file di input è terminato prematuramente senza raggiungere il reale Z_STREAM_END
        if (s.avail_in == 0 && in.eof() && zret == Z_BUF_ERROR) {
            inflateEnd(&s);
            return false;
        }
    }
}
inline fs::path make_output_path(const fs::path& in) {
    // Se siamo in modalità compressione, creiamo il percorso aggiungendo l'estensione (es. .defl) al nome originale
    if (cfg.mode == COMP) {
        return fs::path(in.string() + cfg.out_ext);
    }
    // Se siamo in modalità decompressione, dobbiamo rimuovere l'estensione di compressione
    else {
        const std::string s = in.string();
        const std::string& ext = cfg.out_ext;

        // Controlla se il file di input termina effettivamente con l'estensione corretta (es. .defl)
        if (s.size() >= ext.size() && s.rfind(ext) == s.size() - ext.size()) {
            // Estrae la sotto-stringa escludendo i caratteri dell'estensione finale (ripristina il nome originale)
            return fs::path{s.substr(0, s.size() - ext.size())};
        }
        // Fallback di sicurezza: se il file da decomprimere non ha l'estensione attesa, appende ".raw" per evitare conflitti
        return fs::path{s + ".raw"};
    }
}
}  // namespace

// apply compression or decompression on a regular file
inline bool doWork(const char* path_cstr) {
    const fs::path in(path_cstr);
    /*
        Controlla che il file esista, potrebbe essere stato eliminato durante l'esecuzione del programma
    */
    if (!fs::exists(in)) {
        std::fprintf(stderr, "File disappear: %s\n", path_cstr);
        return false;
    }
    /*
        Se il file esiste controlla che sia effettivamente un file (e non una directory o un link simbolico)
    */
    if (!fs::is_regular_file(in)) {
        if (cfg.verbose) std::fprintf(stderr, "Skip, not a regular file: %s\n", path_cstr);
        return true;
    }

    const fs::path out_final = make_output_path(in);                 // crea il path di destinazione
    const fs::path out_tmp = fs::path(out_final.string() + ".tmp");  // crea il path finale di destinazione temporaneo

    if (cfg.verbose) {
        if (cfg.mode == COMP)
            std::fprintf(stderr, "[C] %s -> %s\n", in.string().c_str(), out_final.string().c_str());
        else
            std::fprintf(stderr, "[D] %s -> %s\n", in.string().c_str(), out_final.string().c_str());
    }
    // controlla che siano diversi
    std::error_code ec_eq;
    if (fs::equivalent(in, out_final, ec_eq) && !ec_eq) {
        std::fprintf(stderr, "Error: Output coincide with input: %s\n", out_final.string().c_str());
        return false;
    }
    // crea lo stream di input
    std::ifstream ifs(in, std::ios::binary);
    if (!ifs) {
        std::fprintf(stderr, "Error: cannot open in input: %s\n", in.string().c_str());
        return false;
    }
    // crea lo stream di output temporaneo
    std::ofstream ofs(out_tmp, std::ios::binary | std::ios::trunc);
    if (!ofs) {
        std::fprintf(stderr, "Error: cannot open in output: %s\n", out_tmp.string().c_str());
        return false;
    }
    // chiama compressione/decompressione
    bool ok = (cfg.mode == COMP) ? compress_stream(ifs, ofs, cfg.level) : decompress_stream(ifs, ofs);
    ofs.close();  // chiude lo stream di output (necessario per poter fare la renameù)

    if (!ok) {
        // avvisa in caso di errore
        std::error_code ecc;
        fs::remove(out_tmp, ecc);  // remove temporary file
        std::fprintf(stderr, "Error: %s on %s\n",
            (cfg.mode == COMP ? "compression" : "decompression"),
            in.string().c_str());
        return false;
    }

    // atomic raname, se non supportata dal FS fa la classica copia
    std::error_code ec;
    fs::rename(out_tmp, out_final, ec);
    if (ec) {  // rename failed
        std::error_code ecw, ecr;
        fs::copy_file(out_tmp, out_final, fs::copy_options::overwrite_existing, ecw);
        fs::remove(out_tmp, ecr);  // remove temporary file
        if (ecw) {
            std::fprintf(stderr, "Error: impossible to write output: %s\n", out_final.string().c_str());
            return false;
        }
    }
    // rimuove eventualmente il file di input
    if (cfg.remove_input) {
        std::error_code ecr;
        fs::remove(in, ecr);
        if (ecr && cfg.verbose)
            std::fprintf(stderr, "Warning: file %s has not been removed (%s)\n",
                in.string().c_str(), ecr.message().c_str());
    }
    return true;
}

// walking the directory recursively (if -r 1)
inline bool walkDir(const char* dir_cstr) {
    // controlla che sia una directory
    std::vector<std::future<bool>> F;  // vettore di future che conserva i risultati
    std::vector<fs::path> fileList;    // Lista dei file da processare
    std::size_t current_size = 0;      // dimensione dei file raccolti nella fileList
    const fs::path root(dir_cstr);
    if (!fs::exists(root) || !fs::is_directory(root)) {
        std::fprintf(stderr, "Non è una directory: %s\n", dir_cstr);
        return false;
    }
    // lambda che itera sui file tramite la dowork
    bool all_ok = true;  // sarà false in caso di fallimento di compressione/decompressione di almeno un file
    auto handleSingle = [](const fs::path p) -> bool {
        if (!should_process(p)) {
            if (cfg.verbose)
                std::fprintf(stderr, "Skip: %s\n", p.string().c_str());
            return true;
        }
        return doWork(p.string().c_str());
    };
    auto handleMultiple = [](const std::vector<fs::path> p) -> bool {
        bool ok = true;
        for (const fs::path& path : p) {
            if (!should_process(path)) {
                if (cfg.verbose)
                    std::fprintf(stderr, "Skip: %s\n", path.string().c_str());
                continue;
            }
            ok &= doWork(path.string().c_str());
        }
        return ok;
    };
    if (cfg.recursive) {
        // itera ricorsivamente sulla directory

        fs::directory_options opts = fs::directory_options::skip_permission_denied;
        for (auto it = fs::recursive_directory_iterator(root, opts),
                  end = fs::recursive_directory_iterator();
            it != end; ++it) {
            std::error_code ec;
            if (it->is_symlink(ec) || it->is_directory(ec)) continue;
            if (it->is_regular_file(ec)) {
                std::size_t file_size = get_file_size((it->path().string().c_str()));
                if (file_size > cfg.file_limit) {
                    F.emplace_back(cfg.pool->submit(handleSingle, it->path()));  // file grande che verrà processato singolarmente
                    std::cout << "Task per file grande partito" << std::endl;
                } else {
                    if (file_size + current_size > cfg.file_limit) {
                        // se la lista di file che voglio affidare al task ha raggiunto la dimensione di un file grande li processo
                        F.emplace_back(cfg.pool->submit(handleMultiple, fileList));
                        std::cout << "Task per file piccoli multipli partito" << std::endl;
                        current_size = 0;
                        fileList.clear();
                    } else {
                        fileList.push_back(it->path());
                        current_size += file_size;
                    }
                }
            }
        }
    } else {
        // itera non ricorsivamente
        for (auto& de : fs::directory_iterator(root, fs::directory_options::skip_permission_denied)) {
            std::error_code ec;
            if (de.is_regular_file(ec)) {
                std::size_t file_size = get_file_size((de.path().string().c_str()));
                if (file_size > cfg.file_limit) {
                    F.emplace_back(cfg.pool->submit(handleSingle, de.path()));  // file grande che verrà processato singolarmente
                    std::cout << "Task per file grande partito" << std::endl;
                } else {
                    if (file_size + current_size > cfg.file_limit) {
                        // se la lista di file che voglio affidare al task ha raggiunto la dimensione di un file grande li processo
                        F.emplace_back(cfg.pool->submit(handleMultiple, fileList));
                        std::cout << "Task per file piccoli multipli partito" << std::endl;
                        current_size = 0;
                        fileList.clear();
                    } else {
                        fileList.push_back(de.path());
                        current_size += file_size;
                    }
                }
            }
        }
    }
    if (fileList.size() > 0) {
        // Se sono avanzati dei file file piccoli una volta finita l'iterazione sulla directory
        F.emplace_back(cfg.pool->submit(handleMultiple, fileList));
        std::cout << "Task per file piccoli multipli partito" << std::endl;
    };
    for (size_t i = 0; i < F.size(); ++i) {
        all_ok &= F[i].get();  // controlla che sia andata bene la compressione/decompressione di tutti i file
    }
    return all_ok;
}
