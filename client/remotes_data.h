#ifndef REMOTES_DATA_H
#define REMOTES_DATA_H


/** adres wezla, wolna pamiec, lista dostepnych plikow, adres mcast w notacji kropkowej**/
typedef std::list<std::tuple<struct sockaddr_in,  uint64_t, std::vector<std::string> , std::string>> remoteFilesList;

/** dane na temat wezlow serwerowych **/
namespace remotesData {
    std::mutex semaphore;

    remoteFilesList remotes;

    void clean_file_lists() {
        std::lock_guard<std::mutex> lck(semaphore);
        for(auto &n: remotes) {
            std::get<2>(n).clear();
        }
    }

    void clear_remotes() {
        std::lock_guard<std::mutex> lck(semaphore);
        remotes.clear();
    }

    remoteFilesList get_remotes_copy() {
        std::lock_guard<std::mutex> lck(remotesData::semaphore);
        return remotes;
    }

    bool file_exists(const std::string &file) {
        std::lock_guard<std::mutex> lck(remotesData::semaphore);
        bool found = false;
        for (auto &v: remotes) {
            auto vec =std::get<2>(v);
            if (std::find(vec.begin(),vec.end(), file) != vec.end()){
                found = true;
                break;
            }
        }
        return found;
    }

}

#endif //REMOTES_DATA_H



