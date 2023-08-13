echo Script build kernel ARM64 suport host arcitecture  AMD64 & X86_64.

#download toolchains
wget https://github.com/Manusiamars123/liquid_kernel_realme_even/releases/download/clang/ipongclang.zip
mkdir ipongclang
mv ipongclang.zip ipongclang
cd ipongclang
unzip ipongclang.zip
cd ..

#membersihkan
echo sabar lagi persiapan
rm -rf out
rm -rf HASIL/**
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

make O=out ARCH=arm64 ucip_defconfig

    PATH="ipongclang/bin:${PATH}" \
      make -j$(nproc --all) O=out \
      ARCH=arm64 \
      LD=ld.lld \
      NM=llvm-nm \
      AR=llvm-ar \
      CC="clang" \
      CLANG_TRIPLE=aarch64-linux-gnu- \
      CROSS_COMPILE=aarch64-linux-gnu- \
      CROSS_COMPILE_ARM32=arm-linux-gnueabi-
                      CONFIG_NO_ERROR_ON_MISMATCH=y \
V=0 2>&1 | tee log.txt

#menarik file image.gz-dtb
mv out/arch/arm64/boot/Image.gz-dtb HASIL/
echo lagi nyarii...
echo ketemu
echo mengcopy
echo Done selemek selentod!
