CXX = g++
CFLAGS = -c

all: run_fft.o fftsg.o fftsg2d.o fft.o # links all object files
	$(CXX) -o run_fft.out run_fft.o fftsg.o fftsg2d.o fft.o

# rule to make .o files from all .cpp files
%.o : %.cpp
	$(CXX) $(CFLAGS) $<

#run_fft.o: run_fft.cpp
#	$(CXX) -c run_fft.cpp -o run_fft.o
#fftsg.o: fftsg.cpp
#	$(CXX) -c fftsg.cpp -o fftsg.o
#fftsg2d.o: fftsg2d.cpp
#	$(CXX) -c fftsg2d.cpp -o fftsg2d.o
#fft.o: fft.cpp fft.h
#	$(CXX) -c fft.cpp -o fft.o

clean:
	rm *.o *.out