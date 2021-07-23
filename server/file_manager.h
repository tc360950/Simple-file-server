#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H
#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <mutex>
#include <fstream>
#include <array>
#include <map>
#include <condition_variable>
#include <vector>
#include <iostream>

  constexpr int __MAX_UDP_PACKET_SIZE__ = 65507;

  class FileManager {
    private:
      /* zajeta przesztrzen */
      uint64_t space;

      uint64_t max_space;

      static const int MAX_FILES = 100;

      int open_files;

      std::string shared_folder;

      /* mapa nazwa pliku (w @shared_folder) -> rozmiar pliku */
      std::map<std::string, uint64_t> files;

      /** @bool czy deskryptor jest zajety **/
      bool fileDescriptors[100];

      /** @true jesli plik zostal usuniety przez watek, podczas gdy byl otwarty przez jakis inny */
      bool isDirty[100];

      std::fstream openFiles[100];

      std::string openFileNames[100];

      std::mutex semaphore;

      std::condition_variable waitForFreeSlot;

      int64_t first_free_storage_token;

      std::map<int64_t, uint64_t> storage_tokens;

      /** Zwarac @true jesli udalo sie zarezerwowac pamiec,
        * Wowczas @token daje prawo jej wykorzystania
        */
      bool reserveSpace(uint64_t bytes, int64_t &token);

      uint64_t indexFiles();

    public:
        uint64_t getStorage() const {
            if (max_space <= space) {return 0;}
            return max_space - space;
        }

        FileManager(uint64_t max_space, std::string sf): max_space{max_space}, open_files{0}, shared_folder{sf}, first_free_storage_token{0} {
        }

        void __init__() {
            this->space = this->indexFiles();
        }

      bool writeToFile(const int fd, char *data, const uint64_t len, int64_t token);

      int createFile(const std::string &fileName,int64_t &token, uint64_t space);

      void closeFile(const int fd);

      void deleteFile(const std::string &fileName);

      void releaseSpace(int64_t token);

      std::vector<std::string> getFileNames(std::string &pattern);

      int openFile(const std::string &file);

      uint64_t read(char *buff, int fd, uint64_t size);
  };




#endif //FILE_MANAGER_H
