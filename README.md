
# How 终端文本编辑器

## 一、项目概述

### 1. 项目简介
`How` 是一款完全使用纯 C 语言从底层手搓的轻量级、高性能终端文本编辑器。项目专为极客和开发者打造，不仅完美适配 **Linux 桌面环境**，同时也深度优化了 **Android Termux** 终端环境以及 **Windows Server** 服务器端。

- 无需复杂的图形界面，纯键盘操作，拥有极低的内存占用与极快的响应速度；
- 原生支持 **C, Python, HTML, JavaScript, PHP, Shell** 脚本以及日志文件(.log)的语法高亮；
- 采用低饱和度现代配色（类似 One Dark），长时间编码不刺眼；
- 彻底解决终端下中文显示错位、光标移动截断字符等痛点；
- 支持区域高亮选中、多步撤销/重做、查找替换、行号跳转等全套现代编辑器功能；
- **完美兼容移动端虚拟键盘**：针对手机端特化了按键响应与防闪烁机制，配合 Termux 即可实现随时随地写代码。

### 2. 页面展示

以下为 HOW 编辑器在手机终端中的实际运行截图：

**启动界面：**
极简几何美学的 ASCII 艺术字，展示项目链接与核心快捷键。

![启动界面截图](https://raw.githubusercontent.com/Liuhao20081209/how/main/jpg/main.jpg)

**代码编辑与高亮：**
基于现代终端调色板，支持多语言语法高亮

![编辑界面截图](https://raw.githubusercontent.com/Liuhao20081209/how/main/jpg/edit.jpg)

**状态与警告：**
底部红底白字警告栏设计，有效防止手滑误退出或覆盖文件

![警告界面截图](https://raw.githubusercontent.com/Liuhao20081209/how/main/jpg/save.jpg)

### 3. 快捷键

受限于终端环境，How 采用 `Ctrl` 组合键进行操作，熟练掌握后极其高效：

| 快捷键 | 功能说明 |
|--------|--------|
| `Ctrl + X` | 退出编辑器（有修改时会触发警告提示） |
| `Ctrl + S` | 保存当前文件（未命名文件会提示输入文件名） |
| `Ctrl + O` | 打开指定路径的文件 |
| `Ctrl + E` | 新建文件并清空当前缓冲区 |
| `Ctrl + R` | 撤销上一步操作 |
| `Ctrl + T` | 重做被撤销的操作 |
| `Ctrl + P` | 切换选中模式（使用方向键扩展选区） |
| `Ctrl + Y` | 复制选中的文本 |
| `Ctrl + U` | 在光标处粘贴剪贴板内容 |
| `Ctrl + D` | 删除选中的文本 |
| `Ctrl + F` | 输入关键字进行查找 |
| `Ctrl + N` | 跳至下一个匹配项 |
| `Ctrl + I` | 查找并替换文本（支持全部替换） |
| `Ctrl + B` | 输入行号进行快速跳转 |
| `Ctrl + G` | 呼出快捷键帮助面板 |

---

## 二、安装

### 1. 从Github上下载到本地

在下载之前，请确保能够连接到Github.com：

**Ubuntu / Debian / Termux：**
```bash
ping -c 3 github.com
```

### 2.安装

确保可以连接到Github后，执行命令下载：

```
wget https://raw.githubusercontent.com/Liuhao20081209/how/main/how -O $PREFIX/bin/how && chmod +x $PREFIX/bin/how

```

3. 运行参数

直接运行下载的二进制文件：

```bash
how              # 打开欢迎启动界面
how my.c         # 直接编辑 my.c，自动开启 C 语法高亮，但是默认位置在：$PREFIX/bin/
how /home/my.py  # 可以携带路径直接编辑 my.py，自动开启 Python 语法高亮
how new_file.txt # 打开不存在的文件时，将默认新建该文件
```

---

三、工作目录与架构

How 采用模块化设计，核心逻辑与语法解析器完全解耦：

目录/文件路径 用途说明
how.c 主程序文件，包含 UI 渲染、按键事件循环与核心编辑逻辑
syntax.h 公共头文件，包含颜色宏定义、UTF-8 处理函数及语法解析器声明
c_syntax.c C 语言语法解析器
py_syntax.c Python 语法解析器
html_syntax.c HTML / CSS / 嵌套 JS 语法解析器
js_syntax.c JavaScript 语法解析器
php_syntax.c PHP 语法解析器
sh_syntax.c Shell 与日志文件语法解析器

四、作者联系方式

QQ邮箱：
3693257606@qq.com
