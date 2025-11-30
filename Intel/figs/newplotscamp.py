#!/usr/bin/python3
# -*- coding: utf-8 -*-
import sys
import os #Para quitar directorios y extensiones de un path
import glob #Para obtner una lista de archivos de un path con wildcards
import numpy as np #Package for scientific computing with Python
import matplotlib.pyplot as plt #Librería gráfica
from subprocess import call #Para llamar a un comando del shell


# Esta función lee los archivos en la lista de archivos que se genera a partir
# de filePath (que tendrá wildcards) y devuelve la media del tiempo de ejecución
def readTimeAvg(filePath):
	fileList = glob.glob(filePath)
	timeAcc = 0.0
	n = len(fileList)
	if n == 0:
		print("No hay archivos %s" % filePath)
		return timeAcc
	for file in fileList:
		f = open(file, 'r')
		tmp = f.readline() # Leo el primer comentario
		timeAcc = timeAcc + float(f.readline()) # Leo el tiempo y lo paso a float
		f.close()
	return timeAcc/len(fileList)

#Parseo los argumentos
if len(sys.argv) == 4:
  tseries = sys.argv[1]
  # Le quito el directorio y el .txt si los tiene
  tseries = os.path.basename(tseries)
  tseries = os.path.splitext(tseries)[0]
  w = int(sys.argv[2])
  legend = int(sys.argv[3])
  print("Plotting results for tseries %s with window size %d and %d legend ..." % (tseries, w, legend))
else:
  print("Uso: ./plotSpeedup.py timeseries windowSize legend")
  exit(-1)


titlesDict = {
             # "power-MPIII-SVF_n180000": "Power",
              "seismology-MPIII-SVE_n180000": "Seismology",
             # "seismology-MPIII-SVE": "Seismology_XL",
             # "e0103_n180000": "ECG",
             # "power-MPIII-SVF": "Power_XL",
             # "penguin_sample_TutorialMPweb": "Penguin",
             # "audio-MPIII-SVD": "Audio",
             # "human_activity-MPIII-SVC": "Human activity",
             # "e0103": "EGC_XL"
}

direc="../results/"
l=(128, 512, 2048, 8192) # 8192 16384
numThreads = (1, 2, 4, 8, 16, 32, 64, 96)
x = range(1,len(numThreads)+1)

lfs = 20 #33 #label font size
ms = 2   #marker size
markers = ('o', '^', 'v', 's', '*', 'p', 'h', '<', '>', '8', 'H', 'D', 'd', None)
mksize = (ms*6,ms*7,ms*7,ms*6,ms*8,ms*7,ms*6,ms*7,ms*6,ms*6,ms*6,ms*6,ms*6)
#colors = plt.cm.Set1(range(ini,fin,int((fin-ini)/len(linesv)) ))
set1 = plt.cm.get_cmap('Set1') #Obtengo el color map




#Extraigo el tiempo del secuencial (se tomará para el secuencial el scamp no-vect con 1 hilo)
for i in l:
  linesv = [
            ["scamp"    , "scamp_%s_w%d_t%d_*"],
            ["TilesDiag L=" + str(i) , "scampTilesDiag_%s_w%d_l"+str(i)+"_t%d_*"],
            ["SpecTilesDiag L=" + str(i) , "specScampTilesDiag_%s_w%d_l"+str(i)+"_t%d_*"],
            ["scampUnprot L=" + str(i) , "scampTilesUnprot_%s_w%d_l"+str(i)+"_t%d_*"],]

  colors = set1(np.linspace(0.0,1,len(linesv))) #Elijo los colores (cambiar ini y fin para variar los colores)
                                                #En blanco y negro (hago la media)

  tSeq = readTimeAvg(direc + (linesv[0][1]%(tseries,w,1)))
  for j in range(len(linesv)):
    timePerTh = []
    # Obtengo el tiempo para cada número de hilos
    for d in numThreads:
      timePerTh.append(readTimeAvg(direc + (linesv[j][1]%(tseries,w,d))))
    print("timePerTh:"+str(timePerTh))
    #Convierto las listas en arrays para hacer cálculos más fácilmente (np.array)
    timePerTh = np.array(timePerTh)
    plt.plot(x, tSeq/timePerTh, color=colors[j], label=linesv[j][0], linewidth=ms,
            marker=markers[j], markeredgecolor=colors[j], markersize=mksize[j],
            markeredgewidth=1)
  
    plt.grid(axis='y', linewidth=1, linestyle="-")
    plt.ylabel('Speedup', fontsize=lfs)
    plt.xlabel('Threads', fontsize=lfs)
    plt.title(titlesDict[tseries] + ' w=' + str(w), fontsize=lfs)
    plt.xticks(x,numThreads,fontsize=lfs*0.8)
    plt.yticks(fontsize=lfs*0.8)
    plt.ylim(top=70)
    if (legend != 0):
      plt.legend(frameon=False, fontsize=lfs*0.9, ncol=1, columnspacing=1)

  ax = plt.gca()
  ax.set_aspect(0.09)
  plt.savefig("./scamp_%s-w%d-l%d.pdf" % (tseries,w,i), format='pdf',bbox_inches='tight')
  plt.savefig("./scamp_%s-w%d-l%d.png" % (tseries,w,i), format='png',bbox_inches='tight')
  plt.clf()
  #call("cp ../z_figs/SU.pdf ../../paper-TPDS/figs/", shell=True)
  #plt.show()
