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
#include <unistd.h> //For getpid(), used to get the pid to generate a unique filename
#include <typeinfo> //To obtain type name as string
#include "lib/barriers.h"
#include "lib/stats.h"
#include <immintrin.h>
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define PATH_RESULTS "./results/"

#define DTYPE double        /* DATA TYPE */
#define ITYPE uint64_t /* INDEX TYPE */

#define ALIGN 64

//RIC Me defino unas macros para reservar memoria alineada
// Uso el operador ## para concatenar algo al nombre de la variable.
// Así creo dos variables al reservar memoria: la que se usará (alineada) y otra para que utilizaré al final para liberar memoria
#define ALIGNED_ARRAY_NEW(_type, _var, _elem, _align)                                                                                             \
  assert(_align >= sizeof(_type) && _elem >= 1);                          /* Compruebo condiciones */                                             \
                                                                          /* Reservo más elementos que elem: align(en bytes)/numbytes de type */  \
  _type *_var##__unaligned = new _type[_elem + _align / sizeof(_type)]; /* Con () inicializamos a 0 -- lo quito*/                                 \
  assert(_var##__unaligned != NULL); /* && _var##__unaligned[0] == 0 && _var##__unaligned[1] == 0);  */                                           \
  /* Hago un casting del puntero con uintptr_t. De esta manera el operador + lo tomará como un número y operará en */                             \
  /* aritmética entera. Si no hiciera el casting, el compilador aplica aritmética de punteros */                                                  \
  /* Luego hago una máscara con todo unos menos log2(align) ceros y dejo los lsb a 0 */                                                           \
  _var = (_type *)(((uintptr_t)_var##__unaligned + _align - 1) & ~(uintptr_t)(_align - 1));                                                       \
  assert(((uintptr_t)_var & (uintptr_t)(_align - 1)) == 0); /* Compruebo que var esté alineado */                                                 \
  /* cout << #_var << "__unaligned: " << hex << _var##__unaligned << "(" << dec << (uintptr_t) _var##__unaligned << ") -> " << #_var << ": " << hex << _var << "(" << dec << (uintptr_t) _var << ")" << endl; */

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
           vector<DTYPE> &df, vector<DTYPE> &dg, DTYPE *profile, ITYPE *profileIndex) //vector<DTYPE> &profile, vector<ITYPE> &profileIndex)
{
  //RIC con la memoria transaccional vamos a intentar no privatizar y acceder al profile y al indexProfile protegiéndolo con una transacción
  // Private structures
  //vector<DTYPE> profile_tmp(profileLength * numThreads);
  //vector<ITYPE> profileIndex_tmp(profileLength * numThreads);
#pragma omp parallel //proc_bind(spread)
  {
    TX_DESCRIPTOR_INIT();
    ITYPE tid = omp_get_thread_num();
    DTYPE covariance, correlation;

//NO ES IMPORTANTE
#ifdef DEBUG
    ITYPE iini, ifin, jini, jfin; //Sólo para imprimir
#endif


    for (ITYPE tileii = 0; tileii < profileLength; tileii += maxTileHeight)
    {
      //Sin protección en el acceso al profile hace falta barrera
    //BEGIN_ESCAPE;
#pragma omp for schedule(dynamic) nowait
      for (ITYPE tilej = tileii; tilej < profileLength; tilej += maxTileWidth)
      {
              //END_ESCAPE;
        //Para recorrer en diagonal los tiles
        ITYPE tilei = tilej - tileii;
        //Para recorrer en el orden de los for
        //ITYPE tilei = tileii;
        ITYPE i = tilei;
        ITYPE j = MIN(MAX(tilei + exclusionZone + 1, tilej), profileLength);


//NO ES IMPORTANTE
#ifdef DEBUG
        iini = i;
        jini = j;
#endif



        for (ITYPE jj = j; jj < MIN(tilej + maxTileWidth, profileLength); jj++)
        {
          //Si i==j ==> Coordenada de la diagonal principal. Sólo se calcula el upper triangle.
          //Si no, el upper triangle tb se calcula
          //Triángulo superior
          covariance = 0;
          for (ITYPE wi = 0; wi < windowSize; wi++){
            covariance += ((tSeries[i + wi] - means[i]) * (tSeries[jj + wi] - means[jj]));
          }
          //CHECK_SPEC(tid, 0);
          correlation = covariance * norms[i] * norms[jj];
          if (correlation > profile[i])
          {
            profile[i] = correlation; //Actúo sobre el array global
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
              covariance += (df[i - 1] * dg[jjj - 1] + df[jjj - 1] * dg[i - 1]);
              correlation = covariance * norms[i] * norms[jjj];
            
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


        /**************************************************************************/
        // Lower triangle
        if (tilei != tilej)
        {
          //Si el tile difiere en sus coordenadas es un tile de interior y se calcula el lower triangle también
          //Triángulo inferior
          ITYPE i = tilei + 1;
          ITYPE j = tilej;


#ifdef DEBUG
          iini = i;
          jini = j;
#endif


          for (ITYPE ii = i; ii < MIN(MIN(tilei + maxTileHeight, j - exclusionZone), profileLength); ii++)
          {
            //Si i==j ==> Coordenada de la diagonal principal. Sólo se calcula el upper triangle.
            //Si no, el upper triangle tb se calcula
            //Triángulo superior
            covariance = 0;
            for (ITYPE wi = 0; wi < windowSize; wi++)
              covariance += ((tSeries[ii + wi] - means[ii]) * (tSeries[j + wi] - means[j]));

            correlation = covariance * norms[ii] * norms[j];
            if (correlation > profile[ii])
            {
              profile[ii] = correlation; //Actúo sobre el array global
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
              covariance += (df[iii - 1] * dg[j - 1] + df[j - 1] * dg[iii - 1]);
              correlation = covariance * norms[iii] * norms[j];
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
      }SB_BARRIER(tid); //Barrera implícita omp si no se pone nowait
#ifdef DEBUG
      cout << "PASADA LA BARRERA -------------------------------------------------" << endl;
#endif
    }LAST_BARRIER(tid);
  }
}

int main(int argc, char *argv[])
{
  try
  {
    // Creation of time meassure structures
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
      cout << "El tamaño del tile no es múltiplo del tamaño de línea de caché. La versión TM puede dar falsos conflictos." << endl;
    }
    numThreads = atoi(argv[4]);
    BARRIER_DESCRIPTOR_INIT(numThreads);

    if(!statsFileInit(argc,argv,numThreads,MAX_XACT_IDS)){
      cout << "Error abriendo o inicializando el archivo de estadísticas." << endl;
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

    /* ------------------------------------------------------------------ */
    /* Read time series file */
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
    //RIC Alineo los vectores del profile y profileIndex para que no haya conflictos por false sharing usando transacciones
    //RIC Habrá que introducier un tamaño de ventana múltiplo de la línea de caché
    //vector<DTYPE> profile(profileLength);
    //vector<ITYPE> profileIndex(profileLength);
    DTYPE *profile = NULL;
    ITYPE *profileIndex = NULL;
    ALIGNED_ARRAY_NEW(DTYPE, profile, profileLength + ALIGN, ALIGN); //Meto profileLength+ALIGN para tener padding por si acaso y evitar false sharing con TM
    ALIGNED_ARRAY_NEW(ITYPE, profileIndex, profileLength + ALIGN, ALIGN);

    //Profile initialization
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

    /***************** Preprocess ******************/
    cout << "[>>] Preprocessing..." << endl;
    tstart = chrono::steady_clock::now();
    preprocess(tSeries, means, norms, df, dg);
    tend = chrono::steady_clock::now();
    telapsed = tend - tstart;
    cout << "[OK] Preprocessing Time:         " << setprecision(2) << fixed << telapsed.count() << " seconds." << endl;
    /***********************************************/

    /******************** SCAMP ********************/
    cout << "[>>] Executing SCAMP..." << endl;
    tstart = chrono::steady_clock::now();
    scamp(tSeries, means, norms, df, dg, profile, profileIndex);
    tend = chrono::steady_clock::now();
    telapsed = tend - tstart;
    cout << "[OK] SCAMP Time:              " << setprecision(2) << fixed << telapsed.count() << " seconds." << endl;
    /***********************************************/

    cout << "[>>] Saving result: " << outfilename << " ..." << endl;
    fstream statsFile(outfilename, ios_base::out);
    statsFile << "# Time (s)" << endl;
    statsFile << setprecision(6) << fixed << telapsed.count() << endl;
    // El tamaño no cambia con el número de threads
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

    if(!dumpStats(telapsed.count())){
      cout << "Error volcando las estadísticas." << endl;
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
