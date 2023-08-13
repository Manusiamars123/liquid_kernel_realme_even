echo Script build kernel ARM64 suport host arcitecture  AMD64 & X86_64.

#env-package
sudo apt update && sudo apt upgrade && sudo apt install nano bc bison ca-certificates curl flex gcc git libc6-dev libssl-dev openssl python-is-python3 ssh wget zip zstd sudo make clang gcc-arm-linux-gnueabi software-properties-common build-essential

#download toolchains
wget https://github.com/Manusiamars123/liquid_kernel_realme_even/releases/download/clang/ipongclang.zip
unzip ipongclang.zip

#membersihkan
echo sabar lagi persiapan
rm -rf out
rm -rf HASIL
make mrproper
echo persiapan selesai
echo .
echo .
echo .
echo membuild kernel.......

#ubah nama kernel dan dev builder
export ARCH=arm64
export KBUILD_BUILD_USER=hot
export LOCALVERSION=hehehhehe

#mulai mengcompile kernel
[ -d "out" ] && rm -rf out  mkdir -p out

make O=out ARCH=arm64 even_kvm_defconfig

    PATH="ipongclang/clang-ipong/bin:${PATH}" \
      make -j$(nproc --all) O=out \
      ARCH=arm64 \
      CC="clang" \
      CLANG_TRIPLE=aarch64-linux-gnu- \
      CROSS_COMPILE=aarch64-linux-gnu- \
      CROSS_COMPILE_ARM32=arm-linux-gnueabi-
                      CONFIG_NO_ERROR_ON_MISMATCH=y \
                      V=0 2>&1 | tee log.txt

#menarik file image.gz-dtb
mkdir HASIL
mv out/arch/arm64/boot/Image.gz-dtb HASIL/
echo lagi nyarii...
echo ketemu
echo mengcopy
echo Done selemek selentod!
