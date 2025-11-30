#!/bin/bash

#Para que funciones el proc_bind(close) de #pragma omp parallel
#hay que definir esta variable de entorno
# threads: Each place contains a hardware thread.
# cores: Each place contains a core. If OMP_PLACES is not set, the default setting is cores.
# num_places: Is the number of places.
export OMP_PLACES=cores

#Series cortas
#benchs=(#"./timeseries/power-MPIII-SVF_n180000.txt 1325"
    #"./timeseries/seismology-MPIII-SVE_n180000.txt 50"
    #"./timeseries/e0103_n180000.txt 500"
    #"./timeseries/penguin_sample_TutorialMPweb.txt 800"
#"./timeseries/audio-MPIII-SVD.txt 200"
#"./timeseries/human_activity-MPIII-SVC.txt 120"
#)



#Series largas: 1.8M de muestras
benchs=("./timeseries/seismology-MPIII-SVE.txt 50"
        "./timeseries/e0103.txt 500"
        "/timeseries/power-MPIII-SVF.txt 1325"
)

hilos="1 2 4 8 16 32 64 128"
n=1
#RIC una tirada con dump stats a 1. Se pone a 0 para las siguientes
dumpStats=0
for (( j=0; j<$n; j++ )); do
    for i in "${benchs[@]}"; do
        for t in $hilos; do
            echo "## $j ## $i $t"
            ./specScampTilesDiag $i 128 $t $dumpStats
            ./specScampTilesDiag $i 512 $t $dumpStats
            ./specScampTilesDiag $i 2048 $t $dumpStats
            ./specScampTilesDiag $i 8192 $t $dumpStats
	    ./scampTilesDiag $i 128 $t $dumpStats
            ./scampTilesDiag $i 512 $t $dumpStats
            ./scampTilesDiag $i 2048 $t $dumpStats
            ./scampTilesDiag $i 8192 $t $dumpStats
            ./scamp $i $t $dumpStats
            ./scampTilesUnprot $i 128 $t $dumpStats
            ./scampTilesUnprot $i 512 $t $dumpStats
            ./scampTilesUnprot $i 2048 $t $dumpStats
            ./scampTilesUnprot $i 8192 $t $dumpStats
        done;
    done;
    dumpStats=0
done;
