CXX = g++
FLUID = fluid
APP = latea

AVX_FLAGS = -march=native -mtune=native -mavx2 -mfma

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

$(APP): $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

latea_ui.cpp latea_ui.h: latea.fl
	$(FLUID) -c latea.fl

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(APP) *.o latea_ui.* ligguf_tq/*.o
