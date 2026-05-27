CXX = g++
FLUID = fluid
APP = latea

# optimized for x86_64 with AVX2; feel free to modify
AVX_FLAGS ?= -march=native -mtune=native -mavx2 -mfma

CXXFLAGS = -Wall -Ofast -fopenmp `fltk-config --use-images --cxxflags` $(AVX_FLAGS)
LDFLAGS = -flto `fltk-config --use-images --ldflags` -pthread -fopenmp

# editor objects
OBJS = latea_ui.o editor.o common.o prefs.o prefs_dlg.o history.o llama_client.o autocomp.o font_dialog.o emb_ai.o
# LiGGUF objects
OBJS += ligguf_tq/common.o ligguf_tq/lil_gguf.o ligguf_tq/lil_math.o ligguf_tq/tokenize.o ligguf_tq/runtime.o

all: $(APP)
.PHONY: all

install:
	./install.sh
.PHONY: install

appimage:
	./bundle.sh
.PHONY: appimage

$(APP): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)
	strip $@

latea_ui.cpp latea_ui.h: latea.fl manual.h icon.h
	$(FLUID) -c latea.fl

manual.h: manual.txt
	sed -e '1i\#define LATEA_HELP_MESSAGE \\' -e 's/"/\\"/g' -e 's/.*/"&\\n" \\/' -e '$$a""' $< > $@

icon.h: latea.png
	echo -n '#define LATEA_ICON {' > $@
	echo 'int main() {for(;;){ int x = fgetc(stdin); if (x==EOF) break; printf("%hu,",x); }}' | $(CXX) -x c -include stdio.h -
	cat $< | ./a.out >> $@
	echo '}' >> $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(APP) *.o latea_ui.* ligguf_tq/*.o *.AppImage
	rm -f manual.h icon.h a.out
	rm -rf build
