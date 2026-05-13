CXX = g++
CXXFLAGS = -Wall -std=c++17 `fltk-config --cxxflags`
LDFLAGS = `fltk-config --ldflags` -pthread
OBJS = latea_ui.o main.o common.o prefs.o history.o llama_client.o autocomplete.o editor.o

all: latea
.PHONY: all

latea_ui.cpp latea_ui.h: latea.fl
	fluid -c latea.fl

latea: $(OBJS)
	$(CXX) -o $@ $(OBJS) $(LDFLAGS)

main.o: main.cpp editor.h
	$(CXX) $(CXXFLAGS) -c main.cpp

common.o: common.cpp common.h
	$(CXX) $(CXXFLAGS) -c common.cpp

prefs.o: prefs.cpp prefs.h common.h
	$(CXX) $(CXXFLAGS) -c prefs.cpp

history.o: history.cpp history.h common.h
	$(CXX) $(CXXFLAGS) -c history.cpp

llama_client.o: llama_client.cpp llama_client.h common.h
	$(CXX) $(CXXFLAGS) -c llama_client.cpp

autocomplete.o: autocomplete.cpp autocomplete.h editor.h common.h latea_ui.h
	$(CXX) $(CXXFLAGS) -c autocomplete.cpp

editor.o: editor.cpp editor.h common.h prefs.h history.h autocomplete.h latea_ui.h
	$(CXX) $(CXXFLAGS) -c editor.cpp

latea_ui.o: latea_ui.cpp latea_ui.h
	$(CXX) $(CXXFLAGS) -c latea_ui.cpp

clean:
	rm -f latea *.o latea_ui.cpp latea_ui.h
