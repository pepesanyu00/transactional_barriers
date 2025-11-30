CFLAGS = -O2  -march=sapphirerapids --static
LFLAGS =  -qopenmp
L2FLAGS = -lm -fopenmp

all: specScampTilesDiag scampTilesDiag scamp scampTilesUnprot

specScampTilesDiag: specScampTilesDiag.cpp lib/barriers.c lib/stats.c
	g++ $(CFLAGS) specScampTilesDiag.cpp lib/barriers.c lib/stats.c -o specScampTilesDiag $(L2FLAGS) -mrtm 

scampTilesDiag: scampTilesDiag.cpp
	g++ $(CFLAGS) scampTilesDiag.cpp -o scampTilesDiag $(L2FLAGS)

scamp: scamp.cpp
	g++ $(CFLAGS) scamp.cpp -o scamp $(L2FLAGS)


scampTilesUnprot: scampTilesUnprot.cpp
	g++ $(CFLAGS) scampTilesUnprot.cpp -o scampTilesUnprot $(L2FLAGS)


clean:
	rm -f specScampTilesDiag
	rm -f scampTilesDiag
	rm -f scamp
	rm -f scampTilesUnprot
	rm -f stats/*
	rm -f results/*
