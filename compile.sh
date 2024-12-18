export PATH=$PATH:/opt/gcc-arm/bin
export CC='/opt/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-gcc'
rm -f support/groovy/groovy.cpp.o
make _AF_XDP=0
rm -f support/groovy/groovy.cpp.o
make _AF_XDP=1
