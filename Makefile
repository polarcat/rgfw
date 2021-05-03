cc = gcc
prefix = game
libdir = $(prefix)lib/src/jni

# config.mk example:
#
#  name = my.super.game
#  title = Super Game
#  suffix = free
#  code = 1
#  version = 0.0.1

include $(libdir)/game/config.mk

ifneq (,$(findstring demo,$(MAKECMDGOALS)))
game = $(name).$(suffix)
type = DEMO_GAME
title +=  ($(suffix))
else
game = $(name)
type = FULL_GAME
endif

rgudir = $(libdir)/rgu
include $(rgudir)/sources.mk

jnidir = $(libdir)
include $(jnidir)/game/sources.mk

resdir = $(libdir)/game/res
assetsdir = $(libdir)/game/assets

src = $(prefix)xcb/main.c $(prefix)xcb/audio.c $(rgusrc) $(gamesrc)

flags = -I. -I$(rgudir)/include -I$(jnidir)
flags += -DASSETS_DIR="\"$(assetsdir)\""
flags += -DGAME_NAME="\"$(name)\""

ifeq (,$(findstring debug,$(MAKECMDGOALS)))
flags += -Wall -Wextra
else
flags += -Wunused-function
flags += -Wunused-variable
flags += -Wunused-label
flags += -Wunused-value
endif

flags += -ggdb
libs = -lxcb -lxcb-keysyms -lEGL -lGLESv2
libs += -ggdb

targets = debug-full debug-demo full demo clean

.PHONY: FORCE debug

debug: FORCE
	$(cc) -o $(game) $(src) $(libs) $(flags) $(CFLAGS)

common:
	@printf "\033[0;32mprepare application manifest\033[0m\n"
	cp -v AndroidManifest.xml.template ${prefix}jni/AndroidManifest.xml
	sed -i 's/PACKAGE_NAME/$(game)/gI' ${prefix}jni/AndroidManifest.xml
	sed -i 's/USER_ID/$(name)/gI' ${prefix}jni/AndroidManifest.xml
	sed -i 's/GAME_NAME/$(title)/gI' ${prefix}jni/AndroidManifest.xml
	cp -v Application.mk.template ${prefix}jni/jni/Application.mk
	sed -i 's/PKG_TYPE/$(type)/gI' ${prefix}jni/jni/Application.mk
	sed -i 's/PKG_NAME/$(game)/gI' ${prefix}jni/jni/Application.mk
	@printf "\033[0;32mcopy resources\033[0m\n"
	cp -r $(assetsdir) $(prefix)jni/
	cp -r $(resdir) $(prefix)jni/
	cp -v build.gradle.template $(prefix)jni/build.gradle
	sed -i 's/VERSION_CODE/$(code)/gI' $(prefix)jni/build.gradle
	sed -i 's/VERSION_NAME/$(version)/gI' $(prefix)jni/build.gradle
	sed -i 's/def tag=1/def tag=1\n\tarchivesBaseName = "$(game)-$$\{versionCode\}-$$\{versionName\}"\n/' $(prefix)jni/build.gradle
	@printf "\033[1;32mstart build\033[0m\n"

full-common:
	sed -i 's/DEMO_GAME/FULL_GAME/gI' ${prefix}jni/jni/Application.mk

demo-common:
	sed -i 's/FULL_GAME/DEMO_GAME/gI' ${prefix}jni/jni/Application.mk

debug-full: FORCE common full-common
	make-gradle

full: FORCE common full-common
	make-gradle-release

debug-demo: FORCE common demo-common
	make-gradle

demo: FORCE common demo-common
	make-gradle-release

clean:
	make-gradle-clean
	@printf "\033[1;32mclean assets\033[0m\n"
	rm -fr ${prefix}jni/assets
	rm -fr ${prefix}jni/.cxx

help:
	@printf "available targets:\n\n"
	@i=1; \
	for target in $(targets); do \
	printf "$$i \033[1;32m$$target\033[0m\n"; i=$$((i+1)); \
	done; \
	echo
