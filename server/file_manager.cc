#include "file_manager.h"
#include <experimental/filesystem>


namespace fs = std::experimental::filesystem;

    uint64_t FileManager::indexFiles(){
        files.clear();

        uint64_t overall_fldr_size = 0;

        const fs::path path_to_dir{this -> shared_folder};

        for (const auto& entry : fs::directory_iterator(path_to_dir)) {
            const auto filename = entry.path().filename().string();
            if (fs::is_regular_file(entry.path())) {
            overall_fldr_size += fs::file_size(entry.path());
            files[filename] = (uint64_t) fs::file_size(entry.path());
            }
        }
        return overall_fldr_size;
    }

      bool FileManager::writeToFile(const int fd, char *data,const uint64_t len, int64_t token){
            std::lock_guard<std::mutex> lck(semaphore);
            if (isDirty[fd]) {
                return false;
            }
            openFiles[fd].write(data,len);
            if (openFiles[fd].bad()) {
                return false;
            } else {
                storage_tokens[token] -= len;
                files[openFileNames[fd]] += len;
                return true;
            }
        }

      /* Tworzy pusty plik o nazwie @fileName i rezerwuje @space bajtow pamieci */
      int FileManager::createFile(const std::string &fileName, int64_t &token, uint64_t space) {
            std::unique_lock<std::mutex> lck(semaphore);
            fs::path path_to_f{this -> shared_folder};
            path_to_f.append(fileName);

            if (files.find(fileName) != files.end() || fs::exists(path_to_f))
                return false;
            if (!reserveSpace(space, token)) {
                return false;
            }
            files[fileName] = 0;

            std::ofstream f(path_to_f,  std::ios::out | std::ios::binary);
            f.close();
            return true;
        }

      void  FileManager::closeFile(const int fd) {
        std::unique_lock<std::mutex> lck(semaphore);
        if (!isDirty[fd])
            openFiles[fd].close();
        fileDescriptors[fd] = false;
        open_files--;
        lck.unlock();
        waitForFreeSlot.notify_one();
    }

        void FileManager::deleteFile(const std::string &fileName) {
            std::lock_guard<std::mutex> lck(semaphore);

            for (int i = 0; i < MAX_FILES; i++) {
                if (openFileNames[i] == fileName) {
                    openFiles[i].close();
                    isDirty[i] = true;
                }
            }
            /** aktualizacja dostepnej pamieci **/
            if (files.find(fileName) != files.end()) {
                space -= files[fileName];
                files.erase(fileName);
            }

            /** get path to file **/
            fs::path path_to_f{this -> shared_folder};
            path_to_f.append(fileName);
            /** remove file **/
            fs::remove(path_to_f);
        }

        bool FileManager::reserveSpace(const uint64_t bytes, int64_t &token) {
            if (space >= max_space) {
                return false;
            }
            uint64_t space_left = max_space - space;
            if (space_left >= bytes) {
                token = first_free_storage_token;
                first_free_storage_token++;
                storage_tokens[token] = bytes;
                space += bytes;
                return true;
            }
            return false;
        }

        void FileManager::releaseSpace(int64_t token) {
            std::lock_guard<std::mutex> lck(semaphore);
            space -= storage_tokens[token];
            storage_tokens.erase(token);
        }

        std::vector<std::string> FileManager::getFileNames(std::string &pattern) {
            std::lock_guard<std::mutex> lck(semaphore);
            std::vector<std::string> fileNames;
            indexFiles();
            for (auto &kv: files) {
                /** file name matches pattern **/
                if (kv.first.find(pattern) != std::string::npos) {
                    fileNames.push_back(kv.first);
                }
            }
            return fileNames;
        }

        int FileManager::openFile(const std::string &f) {
            std::unique_lock<std::mutex> lck(semaphore);
            indexFiles();
            if ( files.find(f) == files.end())
                return -1;
            if (open_files >= MAX_FILES) {
                waitForFreeSlot.wait(lck, [this] {return this->open_files < FileManager::MAX_FILES;} );
            }
            /** find a free descriptor **/
            int fd = 0;
            while (fileDescriptors[fd]) {fd++;};
            /** file might have been removed while we waited for free slot **/
            indexFiles();
            if ( files.find(f) == files.end())
                return -1;

            fs::path path_to_f{this -> shared_folder};
            path_to_f.append(f);
            open_files++;
            fileDescriptors[fd] = true;
            isDirty[fd] = false;
            openFileNames[fd] = f;

            openFiles[fd] = std::fstream(path_to_f, std::ios::out | std::ios::in | std::ios::binary);
            if( !openFiles[fd]) {
                return -1;
            }
            return fd;
        }

        uint64_t FileManager::read(char *buff, int fd, uint64_t size_) {
             std::lock_guard<std::mutex> lck(semaphore);
             std::fstream &file = openFiles[fd];
             if (isDirty[fd]) {
                return 0;
             }
             if (file.read(buff, size_))
                return size_;
            else
                return file.gcount();
        }

