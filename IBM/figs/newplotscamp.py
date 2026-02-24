#!/usr/bin/python3
# -*- coding: utf-8 -*-
# ******************************************************************************
#  * Authors: Jose Sanchez-Yun (pepesy00@uma.es)
#  *          Eladio Gutierrez (eladio@uma.es)
#  *          Ricardo Quislant (quislant@uma.es)
#  *          Oscar Plata (oplata@uma.es)
#  *
#  * University: Dept. of Computer Architecture, University of Malaga,
#  *             Bulevar Louis Pasteur, 35, Malaga, 29071, Andalusia, Spain
#  ******************************************************************************

import sys
import os # For removing directories and extensions from a path
import glob # For obtaining a list of files from a path using wildcards
import numpy as np # Package for scientific computing with Python
import matplotlib.pyplot as plt # Graphic library
from subprocess import call # For invoking a shell command


# This function reads the files in the files list generated from
# filePath (which includes wildcards) and returns the average execution time
def readTimeAvg(filePath):
	fileList = glob.glob(filePath)
	timeAcc = 0.0
	n = len(fileList)
	if n == 0:
		print("No files found %s" % filePath)
		return timeAcc
	for file in fileList:
		f = open(file, 'r')
		tmp = f.readline() # Read first comment
		timeAcc = timeAcc + float(f.readline()) # Read the time and cast to float
		f.close()
	return timeAcc/len(fileList)

# Argument parsing
if len(sys.argv) == 4:
  tseries = sys.argv[1]
  # Remove directory and .txt extension if they exist
  tseries = os.path.basename(tseries)
  tseries = os.path.splitext(tseries)[0]
  w = int(sys.argv[2])
  legend = int(sys.argv[3])
  print("Plotting results for tseries %s with window size %d and %d legend ..." % (tseries, w, legend))
else:
  print("Usage: ./plotSpeedup.py timeseries windowSize legend")
  exit(-1)


titlesDict = {
             # "power-MPIII-SVF_n180000": "Power",
             "seismology-MPIII-SVE_n180000": "Seismology",
             # "seismology-MPIII-SVE": "Seismology",
             # "e0103_n180000": "ECG",
             #"power-MPIII-SVF": "Power_XL",
             # "penguin_sample_TutorialMPweb": "Penguin",
             # "audio-MPIII-SVD": "Audio",
             # "human_activity-MPIII-SVC": "Human activity",
             # "e103": "EGC"
}

direc="../results/"
l=(128, 512, 2048, 8192) # 8192 16384
numThreads = (1, 2, 4, 8, 16, 32, 64, 128)
x = range(1,len(numThreads)+1)

lfs = 20 #33 #label font size
ms = 2   #marker size
markers = ('o', '^', 'v', 's', '*', 'p', 'h', '<', '>', '8', 'H', 'D', 'd', None)
mksize = (ms*6,ms*7,ms*7,ms*6,ms*8,ms*7,ms*6,ms*7,ms*6,ms*6,ms*6,ms*6,ms*6)
#colors = plt.cm.Set1(range(ini,fin,int((fin-ini)/len(linesv)) ))
set1 = plt.cm.get_cmap('Set1') # Retrieve the color map




# Extract sequential time (non-vectorized scamp with 1 thread is used as sequential)
for i in l:
  linesv = [
            ["scamp"    , "scamp_%s_w%d_t%d_*"],
            ["TilesDiag L=" + str(i) , "scampTilesDiag_%s_w%d_l"+str(i)+"_t%d_*"],
            ["SpecTilesDiag L=" + str(i) , "specScampTilesDiag_%s_w%d_l"+str(i)+"_t%d_*"],
            ["scampUnprot L=" + str(i) , "scampTilesUnprot_%s_w%d_l"+str(i)+"_t%d_*"],]

  colors = set1(np.linspace(0.0,1,len(linesv))) # Select colors (change ini and fin to vary colors)
                                                # For black and white (average)

  tSeq = readTimeAvg(direc + (linesv[0][1]%(tseries,w,1)))
  for j in range(len(linesv)):
    timePerTh = []
    # Get time for each number of threads
    for d in numThreads:
      timePerTh.append(readTimeAvg(direc + (linesv[j][1]%(tseries,w,d))))
    print("timePerTh:"+str(timePerTh))
    # Convert lists to arrays for easier calculations (np.array)
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
    plt.ylim(top=25)
    if (legend != 0):
      plt.legend(frameon=False, fontsize=lfs*0.9, ncol=1, columnspacing=1)

  ax = plt.gca()
  ax.set_aspect(0.19)
  plt.savefig("./scamp_%s-w%d-l%d.pdf" % (tseries,w,i), format='pdf',bbox_inches='tight')
  plt.savefig("./scamp_%s-w%d-l%d.png" % (tseries,w,i), format='png',bbox_inches='tight')
  plt.clf()
  #call("cp ../z_figs/SU.pdf ../../paper-TPDS/figs/", shell=True)
  #plt.show()
