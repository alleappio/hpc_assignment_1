#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <omp.h>

/* Include polybench common header. */
#include <polybench.h>

/* Include benchmark-specific header. */
/* Default data type is double, default size is 4000. */
#include "symm.h"

/* Array initialization. */
static void init_array(int ni, int nj,
                       DATA_TYPE *alpha,
                       DATA_TYPE *beta,
                       DATA_TYPE POLYBENCH_2D(C, NI, NJ, ni, nj),
                       DATA_TYPE POLYBENCH_2D(A, NJ, NJ, nj, nj),
                       DATA_TYPE POLYBENCH_2D(B, NI, NJ, ni, nj))
{
  int i, j;

  *alpha = 32412;
  *beta = 2123;
  //#pragma omp target map(tofrom: C[0:ni][0:nj], B[0:ni][0:nj], A[0:nj][0:nj])
   #pragma omp parallel
   {
    
    //#pragma omp parallel for num_threads(4) collapse(2) private(i,j) schedule(static)
    #pragma omp for collapse(2) schedule(static, 8)
    //#pragma omp teams distribute parallel for collapse(2) schedule(static, 8)
    for (i = 0; i < ni; i++)
      for (j = 0; j < nj; j++)
      {
        C[i][j] = ((DATA_TYPE)i * j) / ni;
        B[i][j] = ((DATA_TYPE)i * j) / ni;
      }
    //#pragma omp parallel for collapse(2) private(i,j) schedule(static, 16)
    #pragma omp for collapse(2) schedule(static, 2) 
    //#pragma omp teams distribute parallel for collapse(2) schedule(static, 2)
    for (i = 0; i < nj; i++)
      for (j = 0; j < nj; j++)
        A[i][j] = ((DATA_TYPE)i * j) / ni;
    }  
    
}

/* DCE code. Must scan the entire live-out data.
   Can be used also to check the correctness of the output. */
static void print_array(int ni, int nj,
                        DATA_TYPE POLYBENCH_2D(C, NI, NJ, ni, nj))
{
  int i, j;

  for (i = 0; i < ni; i++)
    for (j = 0; j < nj; j++)
    {
      fprintf(stderr, DATA_PRINTF_MODIFIER, C[i][j]);
      if ((i * ni + j) % 20 == 0)
        fprintf(stderr, "\n");
    }
  fprintf(stderr, "\n");
}
static void kernel_symm_sequential(int ni, int nj,
                                   DATA_TYPE alpha,
                                   DATA_TYPE beta,
                                   DATA_TYPE POLYBENCH_2D(C, NI, NJ, ni, nj),
                                   DATA_TYPE POLYBENCH_2D(A, NJ, NJ, nj, nj),
                                   DATA_TYPE POLYBENCH_2D(B, NI, NJ, ni, nj))
{
  int i, j, k;
  DATA_TYPE acc;
  for (i = 0; i < ni; i++) {
    for (j = 0; j < nj; j++) {
      acc = 0;
      for (k = 0; k < j - 1; k++) {
        C[k][j] += alpha * A[k][i] * B[i][j];
        acc += B[k][j] * A[k][i];
      }
      C[i][j] = beta * C[i][j] + alpha * A[i][i] * B[i][j] + alpha * acc;
    }
  }
}






/* Main computational kernel. The whole function will be timed,
   including the call and return. */
static void kernel_symm(int ni, int nj,
                        DATA_TYPE alpha,
                        DATA_TYPE beta,
                        DATA_TYPE POLYBENCH_2D(C, NI, NJ, ni, nj),
                        DATA_TYPE POLYBENCH_2D(A, NJ, NJ, nj, nj),
                        DATA_TYPE POLYBENCH_2D(B, NI, NJ, ni, nj))
{
  int i, j, k;
  DATA_TYPE acc;
#pragma scop
#pragma omp parallel
  {
/*  C := alpha*A*B + beta*C, A is symetric */
//#pragma omp for private(j, acc, k)
      #pragma omp for collapse(2) private(j, acc,k) schedule(static, 8) 
//#pragma omp for collapse(2) private(i, j, k) reduction(+:acc)
     //#pragma omp target data map(to: A[0:nj][0:nj], B[0:ni][0:nj]) map(tofrom: C[0:ni][0:nj])
      //{
     // #pragma omp target teams distribute parallel for collapse(2) private(i, j, k, acc)
      for (i = 0; i < _PB_NI; i++)
      for (j = 0; j < _PB_NJ; j++)
      {
        acc = 0;
        #pragma omp reduction(+:acc)
        for (k = 0; k < j - 1; k++)
        {
          C[k][j] += alpha * A[k][i] * B[i][j];
          acc += B[k][j] * A[k][i];
        }
        C[i][j] = beta * C[i][j] + alpha * A[i][i] * B[i][j] + alpha * acc;
      }
    //  }
  }
#pragma endscop
}

int main(int argc, char **argv)
{
  /* Retrieve problem size. */
  int ni = NI;
  int nj = NJ;
  double start_time_par_init1, start_time_par_init2, par_time_init1, par_time_init2, start_time1, start_time_seq, seq_time, T_non_parallel_init, T_non_parallel_print, start_time_par, T_non_parallel;
  double PARALLEL_FRACTION, par_time, speedup, num_threads, amdahl_speedup;
 
  /* Variable declaration/allocation. */
  DATA_TYPE alpha;
  DATA_TYPE beta;
  POLYBENCH_2D_ARRAY_DECL(C, DATA_TYPE, NI, NJ, ni, nj);
  POLYBENCH_2D_ARRAY_DECL(A, DATA_TYPE, NJ, NJ, nj, nj);
  POLYBENCH_2D_ARRAY_DECL(B, DATA_TYPE, NI, NJ, ni, nj);
  
  start_time_par_init1 = omp_get_wtime();
  /* Initialize array(s). */
  init_array(ni, nj, &alpha, &beta,
             POLYBENCH_ARRAY(C),
             POLYBENCH_ARRAY(A),
             POLYBENCH_ARRAY(B));
  par_time_init1 = omp_get_wtime() - start_time_par_init1;
  

  //polybench_start_instruments;
  start_time_seq = omp_get_wtime();
  kernel_symm_sequential(ni, nj, alpha, beta, POLYBENCH_ARRAY(C), POLYBENCH_ARRAY(A), POLYBENCH_ARRAY(B));
  //polybench_stop_instruments;
  //polybench_print_instruments;
  seq_time = omp_get_wtime() - start_time_seq;
  printf("Sequential Execution Time: %f seconds\n", seq_time);
 

 /* Initialize array(s). */
  start_time_par_init2 = omp_get_wtime();
  init_array(ni, nj, &alpha, &beta,
             POLYBENCH_ARRAY(C),
             POLYBENCH_ARRAY(A),
             POLYBENCH_ARRAY(B));
  par_time_init2 = omp_get_wtime() - start_time_par_init2;
 

  /* Start timer. */
  //polybench_start_instruments;

  start_time_par = omp_get_wtime();
  /* Run kernel. */
  kernel_symm(ni, nj,
              alpha, beta,
              POLYBENCH_ARRAY(C),
              POLYBENCH_ARRAY(A),
              POLYBENCH_ARRAY(B));
  /* Stop and print timer. */
  //polybench_stop_instruments;
  //polybench_print_instruments;
  par_time = (omp_get_wtime() - start_time_par) + (par_time_init1+par_time_init2);
  printf("Parallel Execution Time: %f seconds\n", par_time); 

 /* Prevent dead-code elimination. All live-out data must be printed
     by the function call in argument. */
  start_time1 = omp_get_wtime();
  polybench_prevent_dce(print_array(ni, nj, POLYBENCH_ARRAY(C)));
  T_non_parallel_init = omp_get_wtime() - start_time1;
  //printf("Total Non-Parallelizable Time: %f seconds\n", T_non_parallel_init);


/* Calculate Parallel Fraction */
// PARALLEL_FRACTION = 1.0 - (T_non_parallel / seq_time);
// printf("Calculated Parallel Fraction: %f\n", PARALLEL_FRACTION);


  /* Calculate Parallel Fraction */
  speedup = seq_time / par_time;
  printf("Speedup: %f\n", speedup);


  num_threads = omp_get_max_threads();  // Max number of threads available
  amdahl_speedup = 1.0 / ((1.0 -  + 0.95) + 0.95 / num_threads);
  printf("Expected Amdahl Speedup: %f\n", amdahl_speedup);

 

  /* Be clean. */
  POLYBENCH_FREE_ARRAY(C);
  POLYBENCH_FREE_ARRAY(A);
  POLYBENCH_FREE_ARRAY(B);
  
  return 0;
}
