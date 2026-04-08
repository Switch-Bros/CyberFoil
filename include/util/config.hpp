#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace inst::config {
    static const std::string appDir = "sdmc:/switch/CyberFoil";
    static const std::string configPath = appDir + "/config.json";
    static const std::string shopsDir = appDir + "/shops";
    static const std::string appVersion = std::string(APP_VERSION);
#ifdef APP_GIT_META
    static const std::string appGitMeta = std::string(APP_GIT_META);
#else
    static const std::string appGitMeta = std::string();
#endif
#ifdef APP_VERSION_FULL
    static const std::string appVersionFull = std::string(APP_VERSION_FULL);
#else
    static const std::string appVersionFull = std::string(APP_VERSION);
#endif

    extern std::string gAuthKey;
    extern std::string lastNetUrl;
    extern std::string offlineDbManifestUrl;
    extern std::string shopUrl;
    extern std::string shopUser;
    extern std::string shopPass;
    extern std::string httpUserAgentMode;
    extern std::string httpUserAgent;
    extern std::vector<std::string> updateInfo;
    extern int languageSetting;
    extern bool ignoreReqVers;
    extern bool validateNCAs;
    extern bool overClock;
    extern bool deletePrompt;
    extern bool autoUpdate;
    extern bool gayMode;
    extern bool soundEnabled;
    extern bool oledMode;
    extern bool mtpExposeAlbum;
    extern bool usbAck;
    extern bool shopHideInstalled;
    extern bool shopHideInstalledSection;
    extern bool shopAllBaseOnly;
    extern bool shopLegacyMode;
    extern bool shopStartGridMode;
    extern bool offlineDbAutoCheckOnStartup;
    extern bool verboseInstallLogging;

    struct ShopProfile {
        std::string fileName;
        std::string protocol;
        std::string host;
        std::string path;
        int port = 8465;
        std::string username;
        std::string password;
        std::string title;
        bool favourite = false;
        std::int64_t updatedAt = 0;
    };

    int DefaultPortForProtocol(const std::string& protocol);
    std::string NormalizeHttpUserAgentMode(const std::string& mode);
    std::string NormalizeShopPath(const std::string& path);
    bool ParseShopUrl(const std::string& rawUrl, std::string& protocol, std::string& host, int& port, std::string& path);
    std::string BuildShopUrl(const ShopProfile& shop);
    std::vector<ShopProfile> LoadShops();
    bool SaveShop(const ShopProfile& shop, std::string* error = nullptr);
    bool DeleteShop(const std::string& fileName);
    bool SetActiveShop(const ShopProfile& shop, bool writeConfig = true);

    void setConfig();
    void parseConfig();
}

