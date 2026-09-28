#pragma once
// AppConfigBuilder — AppConfig 的类型化链式构造器。
// 必填项(命令行参数)是工厂入口,类型上无法遗漏;选填项链式覆盖默认值。
//
// 用法:
//   auto config = liew::AppConfigBuilder::ForArguments(argc, argv)
//                     .WithApplicationName(u"MyApp")
//                     .WithResourcePakName("my_app.pak")
//                     .Build();

#include <string_view>

#include "liew/app/liew_app_config.h"
#include "liew/liew_export.h"

namespace liew {

class LIEW_API AppConfigBuilder {
  public:
    // 必填项入口:进程命令行参数(嵌入自己的 main 时透传)。
    static AppConfigBuilder ForArguments(int argc, const char** argv);

    // 选填项:全部有默认值,按需覆盖。链式调用返回自身引用。
    AppConfigBuilder& WithApplicationName(const char16_t* application_name);
    AppConfigBuilder& WithResourcePakName(std::string_view resource_pak_name);

    // 生成不可变的 AppConfig。Builder 可复用重复 Build。
    AppConfig Build() const;

  private:
    AppConfigBuilder(int argc, const char** argv);

    int argc_;
    const char** argv_;
    const char16_t* application_name_ = u"liew";
    std::string_view resource_pak_name_ = "liew_resources.pak";
};

}  // namespace liew
