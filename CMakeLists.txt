cmake_minimum_required(VERSION 3.21)
project(NightSentinel LANGUAGES C CXX ASM VERSION 0.1.0 DESCRIPTION "Night Sentinel PS1 tech demo")
psn00bsdk_add_executable(nsentinel GPREL main.c)
psn00bsdk_add_cd_image(iso nightsentinel iso.xml DEPENDS nsentinel system.cnf)
