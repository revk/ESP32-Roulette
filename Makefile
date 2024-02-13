#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := Roulette
SUFFIX := $(shell components/ESP32-RevK/buildsuffix)
export SUFFIX

all:	settings.h
	@echo Make: build/$(PROJECT_NAME)$(SUFFIX).bin
	@idf.py build
	@cp build/$(PROJECT_NAME).bin $(PROJECT_NAME)$(SUFFIX).bin
	@echo Done: build/$(PROJECT_NAME)$(SUFFIX).bin

beta:  
	-git pull
	-git submodule update --recursive
	-git commit -a -m checkpoint
	@make set
	cp Roulette*.bin betarelease
	git commit -a -m Beta
	git push

issue:  
	-git pull
	-git submodule update --recursive
	-git commit -a -m checkpoint
	@make set
	cp Roulette*.bin betarelease
	cp Roulette*.bin release
	git commit -a -m Release
	git push

settings.h:     components/ESP32-RevK/revk_settings settings.def components/ESP32-RevK/settings.def
	components/ESP32-RevK/revk_settings $^

components/ESP32-RevK/revk_settings: components/ESP32-RevK/revk_settings.c
	make -C components/ESP32-RevK

set:	s3

s3:
	components/ESP32-RevK/setbuildsuffix -S3-MINI-N4-R2
	@make

flash:
	idf.py flash

monitor:
	idf.py monitor

clean:
	idf.py clean

menuconfig:
	idf.py menuconfig

pull:
	git pull
	git submodule update --recursive

update:
	git submodule update --init --recursive --remote
	-git commit -a -m "Library update"
