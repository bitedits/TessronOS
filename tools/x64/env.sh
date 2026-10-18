# source tools/x64/env.sh — x64 のビルドに使うツールへ PATH を通す(tools/x64/build_toolchain.sh で作ったもの)
PREFIX="${TS_XPACK_PREFIX:-$HOME/.local/xPacks}"
for d in "$PREFIX/x86_64-elf-gcc/bin" "$PREFIX/x86_64-w64-mingw32-gcc/bin"; do
	case ":$PATH:" in
		*":$d:"*) ;;
		*) [ -d "$d" ] && PATH="$d:$PATH" ;;
	esac
done
export PATH
export TS_X64_TOOLCHAIN="$PREFIX/x86_64-elf-gcc/bin"
export TS_X64_EFI_TOOLCHAIN="$PREFIX/x86_64-w64-mingw32-gcc/bin"
unset PREFIX
