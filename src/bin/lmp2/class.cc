/*! \file
    \ingroup LMP2
    \brief Enter brief description of file here
*/

#include "mpi.h"
#include <iostream>
#include <fstream>              // file I/O support
//#include <cstdio>
//#include <cstdlib>
//#include <cstring>
//#include <cmath>
//#include <libipv1/ip_lib.h>
#include <libciomr/libciomr.h>
//#include <libchkpt/chkpt.hpp>
//#include <libpsio/psio.h>
#include <libqt/qt.h>
#include <libparallel/parallel.h>
#include <libmints/basisset.h>
//#include <psifiles.h>
#define EXTERN
#include "globals.h"

    using namespace psi;
    
namespace psi{

extern int myid;
extern int nprocs;

namespace lmp2{

extern int myid_lmp2;
extern int nprocs_lmp2;

    
LMP2::LMP2() {
  fprintf(outfile, "Do not use the default constructor for LMP2");
}

LMP2::~LMP2() {
//  free_block(D);
}

LMP2::LMP2(shared_ptr<PSIO> psio_o, shared_ptr<Chkpt> chkpt_o) {

  psio = psio_o;
  chkpt = chkpt_o;

}

void LMP2::get_moinfo() {

  nso = get_nso();
  natom = get_natom();
  nirreps = get_nirreps();
  nshell = get_nshell();
  C = get_MOC();

  print_moinfo();

  return;
}

void LMP2::opdm() {

  int i;
  int *doccpi;

  doccpi = get_doccpi();

  for(i=0,nocc=0; i < nirreps; i++) nocc += doccpi[i];

  D = block_matrix(nso,nso);
  C_DGEMM('n', 't', nso, nso, nocc, 1, &(C[0][0]), nso, &(C[0][0]), nso, 0, &(D[0][0]), nso);

}

void LMP2::print_moinfo(){

  // A couple of error traps
  if(nirreps != 1) {
    char *symm_label = chkpt->rd_sym_label();
    throw InputException("Local MP2 is only valid in C1 symmetry", symm_label, __FILE__, __LINE__);
  }


  double Enuc = get_enuc();
  double Escf = get_escf();

  if(myid == 0) {
    fprintf(outfile,"\n");
    fprintf(outfile,"\tChkpt Parameters:\n");
    fprintf(outfile,"\t--------------------\n");
    fprintf(outfile,"\tNumber of Irreps    \t= %d\n",nirreps);
    fprintf(outfile,"\tNumber of MOs    \t= %d\n",nso);
    fprintf(outfile,"\n");
    fprintf(outfile,"\tNuclear rep. energy   \t= %20.15f\n",Enuc);
    fprintf(outfile,"\tSCF energy            \t= %20.15f\n",Escf);
    fflush(outfile);
  }
}

void LMP2::get_fock() {

  int i, j;
  double **temp, *X;

  aoF = block_matrix(nso, nso);
  loF = block_matrix(nso, nso);
  X = init_array((nso*nso+nso)/2);
  temp = block_matrix(nso, nso);

  X = chkpt->rd_fock();

  tri_to_sq(X, aoF, nso);
  free(X);

  // Transform the Fock matrix from the AO to LO basis
  C_DGEMM('t', 'n', nso, nso, nso, 1, &(C[0][0]), nso, &(aoF[0][0]), nso, 0, &(temp[0][0]), nso);
  C_DGEMM('n', 'n', nso, nso, nso, 1, &(temp[0][0]), nso, &(C[0][0]), nso, 0, &(loF[0][0]), nso);
  free_block(temp);

}

int* LMP2::get_ij_owner() {

  int i, j, ij, v, count;
  int *ij_owner;

  ij_owner = init_int_array(ij_pairs);

  v=0;
  for(ij=0; ij < ij_pairs; ij++) {
    ij_owner[ij] = v%nprocs;
    v++;
  }

  return &(ij_owner[0]);

}

int* LMP2::get_ij_local() {

  int i, j, ij, v, count;
  int *ij_local;

  ij_local = init_int_array(ij_pairs);

  v=0;
  count=0;
  for(ij=0; ij < ij_pairs; ij++) {
      ij_local[ij] = count;
      if(v%nprocs == nprocs-1) count++;
      v++;
  }

  return &(ij_local[0]);

}

int* LMP2::get_mr_owner(int n) {

  int count, v, num_unique_shells;
  int *mr_owner;

  num_unique_shells = n;

  mr_owner = init_int_array(n);

  v = 0;
  for(count=0; count < num_unique_shells; count++) {
    mr_owner[count] = v%nprocs;
    v++;
  }

  return &(mr_owner[0]);

}

int LMP2::get_mn_pairs(int n) {

  int count, v, num_unique_shells;
  int mn_pairs;

  num_unique_shells = n;

  v = 0;
  for(count=0; count < num_unique_shells; count++) {
    if(myid == v%nprocs) mn_pairs++;
    v++;
  }

  return mn_pairs;
}

int LMP2::get_num_unique_shells() {

    int count = 0;
    for (int M = 0; M < nshell; M++) {
        for (int N = 0; N <= M; N++, count++) {
        }
    }

    return count;
}

int** LMP2::get_MN_shell(shared_ptr<BasisSet> basisset) {

    int ** MN_shell = init_int_matrix(4, num_unique_shells);
    int count = 0;
    for (int M = 0; M < nshell; M++) {
        int numm = basisset->shell(M)->nfunction();
        for (int N = 0; N <= M; N++, count++) {
            int numn = basisset->shell(N)->nfunction();
            MN_shell[0][count] = M;
            MN_shell[1][count] = numm;
            MN_shell[2][count] = N;
            MN_shell[3][count] = numn;
        }
    }

    return MN_shell;
}

int** LMP2::get_ij_map() {

    int i, j, ij;
    int **ij_map_ = init_int_matrix(ij_pairs,2);
    int counter;


    counter = 0;
    for (i = 0, ij=0 ; i < nocc; i++) {
        for (j = 0; j <= i; j++, ij++) {
            if (pairdom_exist[ij]) {
                ij_map_[counter][0] = i;
                ij_map_[counter][1] = j;
                counter++;
            }
        }
    }

    return ij_map_;

}

int* LMP2::original_ij_map() {

    int pairs = (nocc * (nocc + 1)) / 2;
    int i, j, ij;
    int *map_ = init_int_array(pairs);
    int counter;


    counter = 0;
    for (i = 0, ij=0 ; i < nocc; i++) {
        for (j = 0; j <= i; j++, ij++) {
            map_[ij] = counter;
            if (pairdom_exist[ij]) {
                counter++;
            }
        }
    }

    return map_;

}

int **LMP2::compute_pairdomain(int **ij_map_) {

    int m, i, j, ij, a;
    //int **ij_map_ = get_ij_map(&(pairdom_exist_[0]));
    int **pairdomain_ = init_int_matrix(ij_pairs, natom);

    for(m=0; m < ij_pairs; m++) {
        i = ij_map_[m][0];
        j = ij_map_[m][1];
        ij = (i * (i + 1)) / 2 + j;
        if (pairdom_exist[ij]) {
            for (a=0; a < natom; a++) {
                if (domain[i][a] || domain[j][a]) {
                    pairdomain_[m][a] = 1;
                }
            }
        }
    }

    return pairdomain_;
}

int *LMP2::compute_pairdomlen(int **ij_map_) {

    int m, i, j, ij, a;
    //int **ij_map_ = get_ij_map(&(pairdom_exist_[0]));
    int *pairdom_len_ = init_int_array(ij_pairs);

    for(m=0; m < ij_pairs; m++) {
        i = ij_map_[m][0];
        j = ij_map_[m][1];
        ij = (i * (i + 1)) / 2 + j;
        if (pairdom_exist[ij]) {
            for (a=0; a < natom; a++) {
                if (domain[i][a] || domain[j][a]) {
                    pairdom_len_[m] += aostop[a] - aostart[a] + 1;
                }
            }
        }
    }

    return pairdom_len_;
}

}} /* End namespace */


