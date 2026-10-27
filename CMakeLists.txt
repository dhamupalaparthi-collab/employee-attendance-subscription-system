cmake_minimum_required(VERSION 3.10)
project(CompanySubscriptionWebsite)

set(CMAKE_CXX_STANDARD 17)

# Crow headers live under include/ (e.g. crow_all.h); smtp_mail.h is here too.
include_directories(include)

# libcurl's CURLConfig.cmake pulls in OpenSSL 3. You must install the dev package, e.g.:
#   pacman -S mingw-w64-x86_64-openssl
# Then reconfigure (delete CMakeCache.txt in build/ if needed).
if(NOT DEFINED OPENSSL_ROOT_DIR)
    if(EXISTS "C:/msys64/mingw64/include/openssl/ssl.h")
        set(OPENSSL_ROOT_DIR "C:/msys64/mingw64" CACHE PATH "MSYS2 MinGW64")
    elseif(EXISTS "C:/msys64/ucrt64/include/openssl/ssl.h")
        set(OPENSSL_ROOT_DIR "C:/msys64/ucrt64" CACHE PATH "MSYS2 UCRT64")
    elseif(EXISTS "C:/msys64/clang64/include/openssl/ssl.h")
        set(OPENSSL_ROOT_DIR "C:/msys64/clang64" CACHE PATH "MSYS2 Clang64")
    endif()
endif()

# MSYS2 MinGW64 example:
#   pacman -S mingw-w64-x86_64-curl mingw-w64-x86_64-openssl mingw-w64-x86_64-cmake
#   cmake -G "MinGW Makefiles" -DCMAKE_PREFIX_PATH=C:/msys64/mingw64 -S .. -B .
find_package(CURL REQUIRED)

add_executable(app main.cpp smtp_mail.cpp sqlite3.c)

target_link_libraries(app PRIVATE CURL::libcurl)

# Extra Windows sockets/crypto libs (often required when linking libcurl on MinGW)
if (WIN32)
    target_link_libraries(app PRIVATE 
        ws2_32 
        mswsock   # ✅ ADD THIS LINE (IMPORTANT)
        crypt32 
        wldap32 
        normaliz
    )
endif()