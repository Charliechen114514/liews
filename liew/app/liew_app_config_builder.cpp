#include "liew/app/liew_app_config_builder.h"

#include <utility>

namespace liew {

AppConfigBuilder::AppConfigBuilder(int argc, const char** argv) : argc_(argc), argv_(argv) {}

AppConfigBuilder AppConfigBuilder::ForArguments(int argc, const char** argv) {
    return AppConfigBuilder(argc, argv);
}

AppConfigBuilder& AppConfigBuilder::WithApplicationName(const char16_t* application_name) {
    application_name_ = application_name;
    return *this;
}

AppConfigBuilder& AppConfigBuilder::WithResourcePakName(std::string_view resource_pak_name) {
    resource_pak_name_ = resource_pak_name;
    return *this;
}

AppConfig AppConfigBuilder::Build() const {
    AppConfig config(argc_, argv_);
    config.application_name = application_name_;
    config.resource_pak_name = resource_pak_name_;
    return config;
}

}  // namespace liew
