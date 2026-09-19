// views_media 冒烟 — t1 级 (链接 + init), 无 UI。
// 判据: WASAPI 设备枚举 (输出 + 输入), 走 AudioManager→AudioManagerWin→
// MMDevice 真实平台链。打印结果后退出; exit 0 = 通过。
//
// 产品决策 (2026-09-19): 第一版跟 Qt6 Multimedia — 纯平台解码 (MF/DXVA),
// 软件解码族 (ffmpeg/libvpx/dav1d) 不进产品。use_blink=false 时上游的
// media_use_ffmpeg=false clamp 恰好与我们一致, 不需要 patch。
// 解码判据留给 t2 (届时定 MF 路径或开 media_use_ffmpeg=true 让闭包长回来)。
// 注: 这是 views_media 二级产品第一次有自己的 root target。
#include <cstdio>
#include <memory>
#include <vector>

#include "base/at_exit.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/test/allow_check_is_test_for_testing.h"
#include "base/test/task_environment.h"
#include "base/test/test_timeouts.h"
#include "media/audio/audio_device_description.h"
#include "media/audio/audio_manager.h"
#include "media/audio/audio_system_impl.h"
#include "media/audio/audio_thread_impl.h"

int main(int argc, char** argv) {
  base::AtExitManager at_exit;
  base::test::AllowCheckIsTestForTesting();
  base::CommandLine::Init(argc, argv);
  TestTimeouts::Initialize();  // TaskEnvironment 构造即取 action_timeout, 须先备好
  base::test::TaskEnvironment task_environment(
      base::test::TaskEnvironment::MainThreadType::UI);

  int failures = 0;

  // AudioManager 析构链自行收尾 (须在 TaskEnvironment 之前销毁, 作用域保证)。
  auto audio_manager = media::AudioManager::CreateForTesting(
      std::make_unique<media::AudioThreadImpl>());
  media::AudioSystemImpl audio_system(audio_manager.get());

  // AudioSystem 为异步回调面 (答复回投到调用线程), RunLoop 泵到齐。
  auto enumerate = [&audio_system](bool for_input) {
    media::AudioDeviceDescriptions descs;
    base::RunLoop loop;
    audio_system.GetDeviceDescriptions(
        for_input,
        base::BindOnce(
            [](media::AudioDeviceDescriptions* out, base::RunLoop* loop,
               media::AudioDeviceDescriptions d) {
              *out = std::move(d);
              loop->Quit();
            },
            &descs, &loop));
    loop.Run();
    return descs;
  };

  const media::AudioDeviceDescriptions outputs = enumerate(false);
  std::printf("[media-smoke] 1/2 WASAPI 输出设备: %zu 个\n", outputs.size());
  for (const auto& d : outputs)
    std::printf("    - %s [%s]\n", d.device_name.c_str(), d.unique_id.c_str());
  if (outputs.empty()) {
    std::printf("    !! 输出枚举为空\n");
    ++failures;
  }

  const media::AudioDeviceDescriptions inputs = enumerate(true);
  std::printf("[media-smoke] 2/2 WASAPI 输入设备(麦克风): %zu 个\n",
              inputs.size());
  for (const auto& d : inputs)
    std::printf("    - %s [%s]\n", d.device_name.c_str(), d.unique_id.c_str());
  if (inputs.empty()) {
    std::printf("    !! 输入枚举为空 (无声卡环境? 人工确认)\n");
    ++failures;
  }

  // 销毁须回音频线程 (AudioDeviceListenerWin 线程绑定, main 线程析构会被
  // ThreadChecker DCHECK 拦下): 送回销毁 + 泵干, 且先于 TaskEnvironment。
  audio_manager->GetTaskRunner()->PostTask(
      FROM_HERE, base::BindOnce([](std::unique_ptr<media::AudioManager>) {},
                                std::move(audio_manager)));
  task_environment.RunUntilIdle();

  std::printf("[media-smoke] %s (failures=%d)\n", failures ? "FAIL" : "OK",
              failures);
  return failures ? 1 : 0;
}
