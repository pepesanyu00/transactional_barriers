/******************************************************************************
 * Authors: Jose Sanchez-Yun (pepesy00@uma.es)
 *          Eladio Gutierrez (eladio@uma.es)
 *          Ricardo Quislant (quislant@uma.es)
 *          Oscar Plata (oplata@uma.es)
 *
 * University: Dept. of Computer Architecture, University of Malaga,
 *             Bulevar Louis Pasteur, 35, Malaga, 29071, Andalusia, Spain
 ******************************************************************************/
#include <iostream>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <vector>
#include <string>
#include <sstream>
#include <chrono>
#include <assert.h>
#include <omp.h>
#include <unistd.h> // For getpid(), used to generate a unique filename
#include <typeinfo> // To obtain type name as string
#include "lib/barriers.h"
#include "lib/stats.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define PATH_RESULTS "./results/"

#define DTYPE double   // Data type
#define ITYPE uint64_t // Index type

#define ALIGN 64

// Macros for aligned memory allocation
// Uses the ## operator to concatenate to the variable name.
// Creates two variables: the aligned one for usage, and another for freeing memory later.
#define ALIGNED_ARRAY_NEW(_type, _var, _elem, _align)                                                                                             \
  assert(_align >= sizeof(_type) && _elem >= 1);                          /* Check conditions */                                                  \
                                                                          /* Allocate extra elements to ensure alignment */                       \
  _type *_var##__unaligned = new _type[_elem + _align / sizeof(_type)];   /* Allocate unaligned memory */                                         \
  assert(_var##__unaligned != NULL);                                                                                                              \
  /* Cast pointer to uintptr_t to perform integer arithmetic, then mask to align bits */                                                          \
  _var = (_type *)(((uintptr_t)_var##__unaligned + _align - 1) & ~(uintptr_t)(_align - 1));                                                       \
  assert(((uintptr_t)_var & (uintptr_t)(_align - 1)) == 0); /* Check alignment */

#define ALIGNED_ARRAY_DEL(_var)      \
  assert(_var##__unaligned != NULL); \
  delete[] _var##__unaligned;

using namespace std;

ITYPE numThreads, exclusionZone, windowSize, tSeriesLength, profileLength, maxTileWidth, maxTileHeight;

// Computes all required statistics for SCAMP, populating info with these values
void preprocess(vector<DTYPE> &tSeries, vector<DTYPE> &means, vector<DTYPE> &norms,
                vector<DTYPE> &df, vector<DTYPE> &dg)
{

  vector<DTYPE> prefix_sum(tSeries.size());
  vector<DTYPE> prefix_sum_sq(tSeries.size());

  // Calculates prefix sum and square sum vectors
  prefix_sum[0] = tSeries[0];
  prefix_sum_sq[0] = tSeries[0] * tSeries[0];
  for (ITYPE i = 1; i < tSeriesLength; ++i)
  {
    prefix_sum[i] = tSeries[i] + prefix_sum[i - 1];
    prefix_sum_sq[i] = tSeries[i] * tSeries[i] + prefix_sum_sq[i - 1];
  }

  // Prefix sum value is used to calculate mean value of a given window, taking last value
  // of the window minus the first one and dividing by window size
  means[0] = prefix_sum[windowSize - 1] / static_cast<DTYPE>(windowSize);
  for (ITYPE i = 1; i < profileLength; ++i)
    means[i] = (prefix_sum[i + windowSize - 1] - prefix_sum[i - 1]) / static_cast<DTYPE>(windowSize);

  DTYPE sum = 0;
  for (ITYPE i = 0; i < windowSize; ++i)
  {
    DTYPE val = tSeries[i] - means[0];
    sum += val * val;
  }
  norms[0] = sum;

  // Calculates L2-norms (euclidean norm, euclidean distance)
  for (ITYPE i = 1; i < profileLength; ++i)
    norms[i] = norms[i - 1] + ((tSeries[i - 1] - means[i - 1]) + (tSeries[i + windowSize - 1] - means[i])) *
                                  (tSeries[i + windowSize - 1] - tSeries[i - 1]);
  for (ITYPE i = 0; i < profileLength; ++i)
    norms[i] = 1.0 / sqrt(norms[i]);

  // Calculates df and dg vectors
  for (ITYPE i = 0; i < profileLength - 1; ++i)
  {
    df[i] = (tSeries[i + windowSize] - tSeries[i]) / 2.0;
    dg[i] = (tSeries[i + windowSize] - means[i + 1]) + (tSeries[i] - means[i]);
  }
}

void scamp(vector<DTYPE> &tSeries, vector<DTYPE> &means, vector<DTYPE> &norms,
           vector<DTYPE> &df, vector<DTYPE> &dg, DTYPE *profile, ITYPE *profileIndex)
{
  // With transactional memory, we attempt to avoid privatization and access the global profile protected by a transaction.

#pragma omp parallel
  {
    TX_DESCRIPTOR_INIT();
    ITYPE tid = omp_get_thread_num();
    DTYPE covariance, correlation;

#ifdef DEBUG
    ITYPE iini, ifin, jini, jfin; // Only for printing
#endif

    for (ITYPE tileii = 0; tileii < profileLength; tileii += maxTileHeight)
    {
#pragma omp for schedule(dynamic) nowait
      for (ITYPE tilej = tileii; tilej < profileLength; tilej += maxTileWidth)
      {
        // Traverse tiles diagonally
        ITYPE tilei = tilej - tileii;
        ITYPE i = tilei;
        ITYPE j = MIN(MAX(tilei + exclusionZone + 1, tilej), profileLength);

#ifdef DEBUG
        iini = i;
        jini = j;
#endif

        for (ITYPE jj = j; jj < MIN(tilej + maxTileWidth, profileLength); jj++)
        {
          // If i==j ==> Main diagonal coordinate (only compute upper triangle).
          // Otherwise, upper triangle is also computed.
          // Upper triangle computing
          covariance = 0;
          //BEGIN_ESCAPE;
          for (ITYPE wi = 0; wi < windowSize; wi++)
            covariance += ((tSeries[i + wi] - means[i]) * (tSeries[jj + wi] - means[jj]));
          correlation = covariance * norms[i] * norms[jj];
          //END_ESCAPE;
          CHECK_SPEC(tid);
          if (correlation > profile[i])
          {
            profile[i] = correlation; // Updating global array
            profileIndex[i] = jj;
          }
          if (correlation > profile[jj])
          {
            profile[jj] = correlation;
            profileIndex[jj] = i;
          }

          i++;

          for (ITYPE jjj = jj + 1; jjj < MIN(tilej + maxTileWidth, profileLength); jjj++, i++)
          {
            //BEGIN_ESCAPE;
            covariance += (df[i - 1] * dg[jjj - 1] + df[jjj - 1] * dg[i - 1]);
            correlation = covariance * norms[i] * norms[jjj];
	          //END_ESCAPE;
            //CHECK_SPEC(tid);
            if (correlation > profile[i])
            {
              profile[i] = correlation;
              profileIndex[i] = jjj;
            }
            if (correlation > profile[jjj])
            {
              profile[jjj] = correlation;
              profileIndex[jjj] = i;
            }
	          //CHECK_SPEC(tid);

#ifdef DEBUG
            jfin = jjj;
#endif
          }
#ifdef DEBUG
          if (jini == jj)
            ifin = i - 1;
#endif
          i = tilei;
        }
#ifdef DEBUG
        cout << "Upper triangle | tid: " << tid << " tilei(ini,fin): " << iini << "," << ifin << " tilej(ini,fin): " << jini << "," << jfin << endl;
#endif


        // Lower triangle
        if (tilei != tilej)
        {
          // If tile coordinates differ, it is an inner tile and the lower triangle is also computed.
          ITYPE i = tilei + 1;
          ITYPE j = tilej;

#ifdef DEBUG
          iini = i;
          jini = j;
#endif

          for (ITYPE ii = i; ii < MIN(MIN(tilei + maxTileHeight, j - exclusionZone), profileLength); ii++)
          {
            covariance = 0;
            //BEGIN_ESCAPE;
            for (ITYPE wi = 0; wi < windowSize; wi++)
              covariance += ((tSeries[ii + wi] - means[ii]) * (tSeries[j + wi] - means[j]));
            correlation = covariance * norms[ii] * norms[j];
            //END_ESCAPE;

            if (correlation > profile[ii])
            {
              profile[ii] = correlation; // Updating global array
              profileIndex[ii] = j;
            }
            if (correlation > profile[j])
            {
              profile[j] = correlation;
              profileIndex[j] = ii;
            }

            j++;

            for (ITYPE iii = ii + 1; (iii < MIN(MIN(tilei + maxTileHeight, j - exclusionZone), profileLength)) &&
                                     (j < profileLength);
                 iii++, j++)
            {
              //BEGIN_ESCAPE;
              covariance += (df[iii - 1] * dg[j - 1] + df[j - 1] * dg[iii - 1]);
              correlation = covariance * norms[iii] * norms[j];
              //END_ESCAPE;
              if (correlation > profile[iii])
              {
                profile[iii] = correlation;
                profileIndex[iii] = j;
              }
              if (correlation > profile[j])
              {
                profile[j] = correlation;
                profileIndex[j] = iii;
              }
#ifdef DEBUG
              ifin = iii;
#endif
            }
#ifdef DEBUG
            if (iini == ii)
              jfin = j - 1;
#endif
            j = tilej;
          }
#ifdef DEBUG
          cout << "Lower triangle | tid: " << tid << " tilei(ini,fin): " << iini << "," << ifin << " tilej(ini,fin): " << jini << "," << jfin << endl;
#endif
        }
      }
      SB_BARRIER(tid); // Implicit OpenMP barrier if 'nowait' is not specified
#ifdef DEBUG
      cout << "-------------------------" << endl;
#endif
    }
    LAST_BARRIER(tid);
  }
}

int main(int argc, char *argv[])
{
  try
  {
    // Creation of time measure structures
    chrono::steady_clock::time_point tstart, tend;
    chrono::duration<double> telapsed;

    if (argc != 6)
    {
      cout << "usage: " << argv[0] << " input_file win_size tile_size num_threads dump_profile" << endl;
      cout << "       - input_file: ./timeseries/<file_name> " << endl;
      cout << "       - win_size: number between 1 and tseries length - 1" << endl;
      cout << "       - tile_size: size of the tile; preferably multiple of " << ALIGN << endl;
      cout << "       - num_threads: number of threads to spawn" << endl;
      cout << "       - dump_profile: 1 - dump the profile in the csv file; 0 - no dump" << endl;
      return 1;
    }

    windowSize = atoi(argv[2]);
    maxTileWidth = maxTileHeight = atoi(argv[3]);
    if (((maxTileWidth * sizeof(DTYPE)) % ALIGN) != 0)
    {
      cout << "Warning: Tile size is not a multiple of cache line size. The TM version may encounter false sharing conflicts." << endl;
    }
    numThreads = atoi(argv[4]);
    BARRIER_DESCRIPTOR_INIT(numThreads);

    if (!statsFileInit(argc, argv, numThreads)){
      cout << "Error opening or initializing the statistics file." << endl;
      return 0;
    }

    bool dumpProfile = (atoi(argv[5]) == 0) ? false : true;
    // Set the exclusion zone to 0.25
    exclusionZone = (ITYPE)(windowSize * 0.25);
    omp_set_num_threads(numThreads);

    vector<DTYPE> tSeries;
    string inputfilename = argv[1];
    stringstream tmp;
    tmp << PATH_RESULTS << argv[0] << "_" << inputfilename.substr(inputfilename.rfind('/') + 1, inputfilename.size() - 4 - inputfilename.rfind('/') - 1) << "_w" << windowSize << "_l" << maxTileWidth << "_t" << numThreads << "_d" << dumpProfile << "_" << getpid() << ".csv";
    string outfilename = tmp.str();

    // Display info through console
    cout << endl;
    cout << "############################################################" << endl;
    cout << "///////////////////////// " << argv[0] << " ////////////////////////////" << endl;
    cout << "############################################################" << endl;
    cout << endl;
    cout << "[>>] Reading File: " << inputfilename << "..." << endl;

    // Read time series file
    tstart = chrono::steady_clock::now();

    fstream tSeriesFile(inputfilename, ios_base::in);

    DTYPE tempval, tSeriesMin = numeric_limits<DTYPE>::infinity(), tSeriesMax = -numeric_limits<double>::infinity();

    tSeriesLength = 0;
    while (tSeriesFile >> tempval)
    {
      tSeries.push_back(tempval);

      if (tempval < tSeriesMin)
        tSeriesMin = tempval;
      if (tempval > tSeriesMax)
        tSeriesMax = tempval;
      tSeriesLength++;
    }
    tSeriesFile.close();
    tend = chrono::steady_clock::now();
    telapsed = tend - tstart;
    cout << "[OK] Read File Time: " << setprecision(2) << fixed << telapsed.count() << " seconds." << endl;

    // Set Matrix Profile Length
    profileLength = tSeriesLength - windowSize + 1;

    // Auxiliary vectors
    vector<DTYPE> norms(profileLength), means(profileLength), df(profileLength), dg(profileLength);

    // Align the profile and profileIndex vectors to mitigate false sharing conflicts in TM
    // Window size must be a multiple of the cache line
    DTYPE *profile = NULL;
    ITYPE *profileIndex = NULL;
    ALIGNED_ARRAY_NEW(DTYPE, profile, profileLength + ALIGN, ALIGN); // Added margin of ALIGN to pad and avoid false sharing
    ALIGNED_ARRAY_NEW(ITYPE, profileIndex, profileLength + ALIGN, ALIGN);

    // Profile initialization
    for (ITYPE i = 0; i < profileLength; i++) {
      profile[i] = -numeric_limits<DTYPE>::infinity();
    }

    // Display info through console
    cout << endl;
    cout << "------------------------------------------------------------" << endl;
    cout << "************************** INFO ****************************" << endl;
    cout << endl;
    cout << " Series/MP data type: " << typeid(tSeries[0]).name() << "(" << sizeof(tSeries[0]) << "B)" << endl;
    cout << " Index data type:     " << typeid(profileIndex[0]).name() << "(" << sizeof(profileIndex[0]) << "B)" << endl;
    cout << " Time series length:  " << tSeriesLength << endl;
    cout << " Window size:         " << windowSize << endl;
    cout << " Tile size:           " << maxTileWidth << endl;
    cout << " Dump profile:        " << dumpProfile << endl;
    cout << " Time series min:     " << tSeriesMin << endl;
    cout << " Time series max:     " << tSeriesMax << endl;
    cout << " Number of threads:   " << numThreads << endl;
    cout << " Exclusion zone:      " << exclusionZone << endl;
    cout << " Profile length:      " << profileLength << endl;
    cout << "------------------------------------------------------------" << endl;
    cout << endl;

    cout << "[>>] Preprocessing..." << endl;
    tstart = chrono::steady_clock::now();
    preprocess(tSeries, means, norms, df, dg);
    tend = chrono::steady_clock::now();
    telapsed = tend - tstart;
    cout << "[OK] Preprocessing Time:         " << setprecision(2) << fixed << telapsed.count() << " seconds." << endl;

    cout << "[>>] Executing SCAMP..." << endl;
    tstart = chrono::steady_clock::now();
    scamp(tSeries, means, norms, df, dg, profile, profileIndex);
    tend = chrono::steady_clock::now();
    telapsed = tend - tstart;
    cout << "[OK] SCAMP Time:              " << setprecision(2) << fixed << telapsed.count() << " seconds." << endl;

    cout << "[>>] Saving result: " << outfilename << " ..." << endl;
    fstream statsFile(outfilename, ios_base::out);
    statsFile << "# Time (s)" << endl;
    statsFile << setprecision(6) << fixed << telapsed.count() << endl;
    // Memory footprint tracking
    statsFile << "# Mem(KB) tseries,means,norms,df,dg,profile,profileIndex,Total(MB)" << endl;
    statsFile << setprecision(2) << fixed <<(sizeof(DTYPE) * tSeries.size()) / 1024.0f << "," << (sizeof(DTYPE) * means.size()) / 1024.0f << "," <<
                 (sizeof(DTYPE) * norms.size()) / 1024.0f << "," << (sizeof(DTYPE) * df.size()) / 1024.0f << "," <<
                 (sizeof(DTYPE) * dg.size()) / 1024.0f << "," << (sizeof(DTYPE) * profileLength) / 1024.0f << "," <<
                 (sizeof(ITYPE) * profileLength) / 1024.0f << "," <<
                 ((sizeof(DTYPE) * tSeries.size()) / 1024.0f + (sizeof(DTYPE) * means.size()) / 1024.0f +
                 (sizeof(DTYPE) * norms.size()) / 1024.0f + (sizeof(DTYPE) * df.size()) / 1024.0f +
                 (sizeof(DTYPE) * dg.size()) / 1024.0f + (sizeof(DTYPE) * profileLength) / 1024.0f +
                 (sizeof(ITYPE) * profileLength) / 1024.0f) / 1024.0f << endl;
    statsFile << "# Profile Length" << endl;
    statsFile << profileLength << endl;
    if (dumpProfile)
    {
      statsFile << "# i,tseries,profile,index" << endl;
      for (ITYPE i = 0; i < profileLength; i++)
      {
        statsFile << i << "," << setprecision(numeric_limits<DTYPE>::max_digits10) << tSeries[i] << "," << (DTYPE)sqrt(2 * windowSize * (1 - profile[i])) << "," << profileIndex[i] << endl;
      }
    }
    statsFile.close();
    cout << endl;

    if (!dumpStats(telapsed.count(), 1)){
      cout << "Error dumping statistics." << endl;
    }

    ALIGNED_ARRAY_DEL(profile);
    ALIGNED_ARRAY_DEL(profileIndex);
    return 0;
  }
  catch (exception &e)
  {
    cout << "Exception: " << e.what() << endl;
  }
}
