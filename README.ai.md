# rime-llm · Weasel AI 校准候选

基于 [rime/weasel](https://github.com/rime/weasel) fork 的框架级 AI 校准输入功能：
在候选列表末尾追加一个由 OpenAI 兼容接口（`POST /v1/chat/completions`）生成的
「AI校准」候选，通过快捷键手动触发。

详细产品/技术分析见本仓库 `docs/` 与原分析报告。

## 仓库结构

```text
weasel 主体（fork 自 rime/weasel，commit d73f6295）
├── librime/                    # librime 子模块（commit 1c233581）
├── librime-plugins/
│   └── rime-weasel-ai/         # ★ AI 校准合并式插件（主仓库维护）
│       ├── CMakeLists.txt
│       ├── src/                # processor / translator / filter / HTTP / JSON / config
│       ├── test/               # 单元测试 + OpenAI 兼容 mock 服务
│       └── example/            # 用户配置示例
├── inject-plugins.bat          # 构建前把主仓库插件注入 librime/plugins/
└── .github/workflows/build-ai.yml  # ★ CI：云端编译带 AI 模块的 rime.dll
```

设计说明：Windows 版 librime 不支持运行时 DLL 插件加载（`plugins_module.cc`
的 `_WIN32` 分支为 TODO），因此 AI 能力以**合并式插件**编进 `rime.dll`。
插件源码放在主仓库 `librime-plugins/`（子模块内文件无法被主仓库版本控制），
构建前由 `inject-plugins.bat` 复制到 `librime/plugins/`。

## 快速获取 rime.dll（GitHub Actions，推荐）

1. 本仓库 → Actions → **Build Weasel AI rime.dll** → Run workflow
2. 等待构建完成（首次约 15～25 分钟，Boost 有缓存后更快）
3. 在 run 页面 Artifacts 下载：
   - `rime-ai-x64` —— 64 位 `rime.dll`（绝大多数用户用这个）
   - `rime-ai-win32` —— 32 位 `rime.dll`

## 替换已安装小狼毫的 rime.dll

```bat
taskkill /f /im WeaselServer.exe
cd /d "<你的小狼毫安装目录>"        rem 注册表 HKLM\SOFTWARE\Rime\Weasel 的 WeaselRoot
copy rime.dll rime.dll.bak         :: 备份，回滚就靠它
copy /y <下载的rime.dll> .
WeaselServer.exe
```

## 启用 AI 校准

把 `librime-plugins/rime-weasel-ai/example/ai_correction.custom.yaml` 的内容
合并进 `%AppData%\Rime\default.custom.yaml`：

- 把示例中三行 `luna_pinyin/...` 换成你的方案 id（雾凇拼音为 `rime_ice` 等）；
- 填入 OpenAI 兼容服务的 `base_url` / `model` / `api_key`
  （云端如 `https://api.openai.com`；本地如 Ollama `http://127.0.0.1:11434`）；
- 开始菜单 →「【小狼毫】重新部署」。

使用：输入 ≥6 字母拼音后按 `Ctrl+`` `，候选列表最后出现带「AI校准」注释的
候选，正常翻页/点选提交。AI 服务不可用时普通输入完全不受影响。

## 本地构建（可选）

需要 Visual Studio 2022（含 MSVC v143、Windows SDK）：

```bat
cd librime
copy ..\env.vs2022.bat env.bat
set ARCH=x64
..\inject-plugins.bat
build.bat deps release
build.bat release
rem 产物: librime\dist\lib\rime.dll
```

## 开发进度

- ✅ Phase 0/1：框架调研 + 候选链路代码（已通过单元测试与 ASan）
- ⏳ Phase 2：真机联调（本仓库 CI 产物 + mock 服务）
- ⏳ Phase 3+：缓存/上下文/异步刷新（见分析报告 PRD）

## 许可证

- weasel：GPLv3（跟随上游）
- librime：BSD 3-Clause（跟随上游）
- librime-plugins/rime-weasel-ai：随本仓库发布，遵循上游兼容许可

## 自定义 AI 提示词

提示词支持三级配置（优先级从高到低）：

1. **独立文本文件**（推荐）：`ai_correction/prompt_file: ai_prompt.txt`
   - 相对路径基于 Rime 用户目录（`%AppData%\Rime` 或注册表 `RimeUserDir` 指定的目录）
   - 用记事本编辑即可，**改完只需「重新部署」，不用重新编译 DLL**
   - 支持 UTF-8（含 BOM）与换行，上限 8KB
2. **配置内联**：`ai_correction/system_prompt: '一行提示词'`
3. **程序内置默认**：内置提示词已包含"逐音节自查、禁止编造拼音中没有的字词、已有候选完全匹配时直接返回"等规则

参考提示词见 `librime-plugins/rime-weasel-ai/example/ai_prompt.txt`。

## 推理模型（reasoning model）注意事项

如果使用 spark / deepseek-r1 这类带思考的模型，必须设置：

```yaml
ai_correction/reasoning_effort: none   # 关闭思考
ai_correction/max_tokens: 256
```

否则思考会吃掉全部 token 导致 `content` 为空（实测 16 秒且无输出）。注意
`reasoning_effort` 取值 `low`/`minimal` **无效**——思考文本会混进 `content`，
导致 JSON 解析失败。

## AI 候选显示位置

```yaml
ai_correction/candidate_position: page1_end   # 第一页最后一格（默认 last = 整个列表末尾）
ai_correction/deduplicate: false              # 默认 false = 始终显示（即使与内置候选相同）
```
