# vi for xv6-riscv

这是一个简单的`vi`移植于[xv6](https://github.com/nyuichi/xv6)

## run

```bash
$ cd xv6-riscv
$ make qemu
$ vi or vi filename
```

## Support features

- 默认模式
  - `h`：光标左移
  - `j`：光标下移
  - `k`：光标上移
  - `l`：光标右移
  - `d`：删除光标所在当前行，并将当前行内容记入缓冲区
  - `p`：粘贴有`d`命令删除的行
  - `x`：删除光标所在处的字符
  - `i`：进入插入模式
  - `/`：键入字符串向后查找，以回车结束输入
  - `?`：键入字符串向前查找，以回车结束输入
  - `n`：查找下一个单词
  - `N`：查找上一个单词
  - `CTRL + L`：进入c语言语法高亮模式(仅支持部分关键字)
- 插入模式
  - `backspace`: 支持退格键
  - `enter`：支持回车键
  - `esc`：返回默认模式
  - `TAB`：默认4个空格
- 命令行(在默认模式下按`:`进入，按`ESC`退出)
  - `:q`：退出
  - `:w`：保存到进入`vi`时输入的文件名，如果没有则默认文件名为`default.viout`
  - `:e filename`：加载文件到`vi`中

## Bugs

- 由于使用的正则库对`\b`适配较差，导致语法高亮存在一定问题
- 由于渲染高亮文本中采用逐个字符渲染的方式，一些特殊字符在高亮模式下可能存在乱码的问题

## Modified files

- `kernel/console.c`
- `kernel/defs.h`
- `kernel/syscall.c`
- `kernel/syscall.h`
- `kernal/sysfile.c`
- `user/ulib.c`
- `user/user.h`
- `user/usys.pl`
- `user/vi.c`
- `user/re.c`
- `user/re.h`
- `Makefile`

## 本项目使用的开源库

- [xv6-riscv](https://github.com/mit-pdos/xv6-riscv)
- [tiny-regex-c](https://github.com/kokke/tiny-regex-c)
- [nyuichi/xv6](https://github.com/nyuichi/xv6)

## 致谢

感谢下列开源库为作者提供参考：

- [hg-xv6-with-vi](https://github.com/appwhy/hg-xv6-with-vi)
- [xv6-vi](https://github.com/yaodio/xv6-vi)(该项目实现的xv6-vi功能最完善)
