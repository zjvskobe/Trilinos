#include "ml_agg_reitzinger.h"

int  ML_Gen_MGHierarchy_UsingReitzinger(ML *ml_edges, ML* ml_nodes, 
                    int fine_level, int incr_or_decrease,
                    ML_Aggregate *ag, ML_Operator *Tmat,
                    ML_Operator *Tmat_trans,
                    ML_Operator ***Tmat_array,
                    ML_Operator ***Tmat_trans_array,
                    int smooth_flag, double smooth_factor)
{
  int coarsest_level, counter, Nghost, i, *Tcoarse_bindx = NULL;
  int *Tcoarse_rowptr, nz_ptr, row_length, j, *bindx = NULL;
  double *Tcoarse_val = NULL, *node2proc, *val = NULL;
  int allocated = 0, lower;
  ML_Operator *Kn_coarse, *Rn_coarse, *Tcoarse, *Pn_coarse;
  ML_Operator *Pe, *Tcoarse_trans, *Tfine;
  struct ML_CSR_MSRdata *csr_data;
  int Nlevels_nodal, grid_level;
  int created_ag_obj = 0;
  /*
  double *fido,*yyy, *vvv, dtemp;
  struct aztec_context *temp;
  */
  int nzctr;

  if (incr_or_decrease != ML_DECREASING)
    pr_error("Hiptmair: Only ML_DECREASING is supported\n");


  if (ag == NULL)
  {
     created_ag_obj = 1;
     ML_Aggregate_Create(&ag);
     ML_Aggregate_Set_CoarsenScheme_MIS(ag);
     ML_Aggregate_Set_DampingFactor(ag,0.0);
     ML_Aggregate_Set_Threshold( ag, 0.0);
  }
  Tfine = Tmat;
  /********************************************************************/
  /* Set up the operators corresponding to regular unsmoothed         */
  /* aggregation on the nodal matrix.                                 */
  /*------------------------------------------------------------------*/

  Nlevels_nodal = ML_Gen_MGHierarchy_UsingAggregation(ml_nodes, fine_level, 
                                            ML_DECREASING, ag);
  coarsest_level = fine_level - Nlevels_nodal + 1; 

  *Tmat_array = (ML_Operator **) ML_allocate(sizeof(ML_Operator *)*
                         ml_nodes->ML_num_levels);
  *Tmat_trans_array = (ML_Operator **) ML_allocate(sizeof(ML_Operator *)*
                         ml_nodes->ML_num_levels);
  for (i = 0; i < ml_nodes->ML_num_levels; i++)
  {
     (*Tmat_array)[i] = NULL;
     (*Tmat_trans_array)[i] = NULL;
  }
  (*Tmat_array)[fine_level] = Tfine;
  (*Tmat_trans_array)[fine_level] = Tmat_trans;

  /********************************************************************/
  /*                 Build T on the coarse grid.                      */
  /*------------------------------------------------------------------*/

  /* main loop through grid hierarchy */
  for (grid_level = fine_level-1; grid_level >= coarsest_level ;grid_level--)
  {
     Kn_coarse = &(ml_nodes->Amat[grid_level]);
     Rn_coarse = &(ml_nodes->Rmat[grid_level+1]);
   
     if (Kn_coarse->getrow->pre_comm != NULL)
        Nghost = Kn_coarse->getrow->pre_comm->total_rcv_length;
     else Nghost = 0;
   
     /* node2proc = node to processor lookup table
        Main idea:
        
        1) Record the proc. id of all nodes (local + ghost) on the processor.
        2) Exchange boundary information.  Ghost nodes should now have the
           proc.  id of the "owning" processor.
   
        Tcoarse must be constructed in a way that processors do not duplicate
        work.  A processor owns a row if both nodes are local.  If one node
        is a ghost node, than the processor owns row i (edge i with endpoints
        k & j) if proc_id(k) < proc_id(j).  Otherwise, processor proc_id(j)
        will own row i.
     */
   
     node2proc = (double *) ML_allocate(sizeof(double)*(Nghost+
                            Kn_coarse->invec_leng+1));
     for (i = 0; i < Kn_coarse->invec_leng+Nghost; i++)
        node2proc[i] = (double) Kn_coarse->comm->ML_mypid;
   
     if (Kn_coarse->getrow->pre_comm != NULL)
     ML_exchange_bdry(node2proc, Kn_coarse->getrow->pre_comm,
                      Kn_coarse->outvec_leng, Kn_coarse->comm,
                      ML_OVERWRITE,NULL);
   
#ifdef DEBUG_T_BUILD
     printf("\n\n%d: Kn_coarse->N_nonzeros = %d "
            "Kn_coarse->invec_leng+Nghost = %d\n"
            "Kn_coarse->invec_leng = %d\n\n",
            Kn_coarse->comm->ML_mypid, Kn_coarse->N_nonzeros,
            Kn_coarse->invec_leng+Nghost, Kn_coarse->invec_leng);
#endif /* ifdef DEBUG_T_BUILD */

     Tcoarse_bindx =(int *)
                    ML_allocate( 2*Kn_coarse->N_nonzeros *sizeof(int) );
     Tcoarse_val = (double *)
                    ML_allocate( 2*Kn_coarse->N_nonzeros *sizeof(double) );
     Tcoarse_rowptr= (int *)
                     ML_allocate(Kn_coarse->N_nonzeros *sizeof(int));
     Tcoarse_rowptr[0] = 0;
     counter = 0; nz_ptr = 0;
     nzctr = 0;
     for (i = 0; i < Kn_coarse->outvec_leng; i++)
     {
        ML_get_matrix_row(Kn_coarse,1, &i,&allocated,
                          &bindx,&val,&row_length, 0);
        ML_az_sort(bindx, row_length, NULL, NULL);
        nzctr += row_length;
   
        /* Step through unknowns bindx[j] connected to unknown i. */
        for (j = 0; j < row_length; j++)
        {
          {
             /* If nodes i and bindx[j] are owned by same processor ... */
             if (node2proc[i] == node2proc[bindx[j]])
             {
                if (bindx[j] > i)
                {
                   Tcoarse_bindx[nz_ptr]  =  bindx[j];
                   Tcoarse_val[nz_ptr++]  =  1.;
                   Tcoarse_bindx[nz_ptr]  =  i;
                   Tcoarse_val[nz_ptr++]  = -1.;
                   Tcoarse_rowptr[counter+1] = nz_ptr;
                   counter++;
                }
             }
             /* If node i is owned by a smaller processor than
                node bindx[j] ... */
             else if (node2proc[i] < node2proc[bindx[j]])
             {
                Tcoarse_bindx[nz_ptr]  =  bindx[j];
                Tcoarse_val[nz_ptr++]  =  1.;
                Tcoarse_bindx[nz_ptr]  =  i;
                Tcoarse_val[nz_ptr++]  = -1.;
                Tcoarse_rowptr[counter+1] = nz_ptr;
                counter++;
             }
          }
            
        }
     }

     if (nzctr > Kn_coarse->N_nonzeros && Kn_coarse->comm->ML_mypid == 0)
     {
        printf("\n\nML_Gen_MGHierarchy_UsingReitzinger: Not enough space"
               " allocated to build T.\n\n");
        exit(1);
     }
#ifdef DEBUG_T_BUILD
     else
        if (Kn_coarse->comm->ML_mypid == 0 && grid_level == 7)
        printf("%d (%d): ieqj = %d,\n"
               "Kn_coarse->N_nonzeros = %d, "
               "expected nnz (calc. from bindx) = %d,\n"
               "actual nnz(Tcoarse) = %d,\n"
               " invec_leng = %d,"
               " nghost = %d\n",
               Kn_coarse->comm->ML_mypid,
               grid_level,
               ieqj,
               Kn_coarse->N_nonzeros,
               nzctr,
               nz_ptr,
               Kn_coarse->invec_leng,
               Nghost);
     fflush(stdout);
#endif /* ifdef DEBUG_T_BUILD */

     ML_free(node2proc);
   
     csr_data = (struct ML_CSR_MSRdata *) ML_allocate(sizeof(struct 
                                 ML_CSR_MSRdata));
     csr_data->columns = Tcoarse_bindx;
     csr_data->values  = Tcoarse_val;
     csr_data->rowptr  = Tcoarse_rowptr;
   
     Tcoarse = ML_Operator_Create(ml_edges->comm);
     Tcoarse->data_destroy = ML_CSR_MSRdata_Destroy;
     ML_Operator_Set_ApplyFuncData( Tcoarse, Kn_coarse->outvec_leng, counter, 
                                     ML_EMPTY, csr_data, counter, NULL, 0);
     ML_Operator_Set_Getrow(Tcoarse, ML_EXTERNAL, counter, CSR_getrows);
     ML_Operator_Set_ApplyFunc(Tcoarse, ML_INTERNAL, CSR_matvec);
   
     ML_CommInfoOP_Clone(&(Tcoarse->getrow->pre_comm),
                         ml_nodes->Amat[grid_level].getrow->pre_comm);
     (*Tmat_array)[grid_level] = Tcoarse;
   
     Pn_coarse = &(ml_nodes->Pmat[grid_level]);
     Rn_coarse = &(ml_nodes->Rmat[grid_level+1]);
   
     /********************************************************************/
     /* Fix P and R so that they are not normalized. This is so that we  */
     /* can use a matrix triple product combined with post-processing to */
     /* generate Pe. The general idea is that the matrix                 */
     /*                        T_h P_n T_H^*                             */
     /* is almost Pe. If we make sure that P_n contains 1's and -1's, the*/
     /* matrix triple product will yield a matrix with +/- 1 and +/- 2's.*/
     /* If we remove all the 1's and divide the 2's by 2. we arrive at Pe*/
     /*------------------------------------------------------------------*/
   
     /* We want all +1 entries to avoid changing sign of entries in Pe. */
     Pn_coarse = &(ml_nodes->Pmat[grid_level]);
     csr_data = (struct ML_CSR_MSRdata *) Pn_coarse->data;
     for (i = 0; i < csr_data->rowptr[Pn_coarse->outvec_leng]; i++)
       if (csr_data->values[i] != 0) csr_data->values[i] = 1.;
   
     Rn_coarse = &(ml_nodes->Rmat[grid_level+1]);
     csr_data = (struct ML_CSR_MSRdata *) Rn_coarse->data;
   
     for (i = 0; i < csr_data->rowptr[Rn_coarse->outvec_leng]; i++)
       if (csr_data->values[i] != 0) csr_data->values[i] = 1.;
   
     /********************************************************************/
     /* Create Tcoarse_trans.                                            */
     /*------------------------------------------------------------------*/
   
     Tcoarse_trans = ML_Operator_Create(ml_edges->comm);
     ML_Operator_Transpose_byrow(Tcoarse, Tcoarse_trans);
     (*Tmat_trans_array)[grid_level] = Tcoarse_trans;
   
     /********************************************************************/
     /* Here is some code that might work someday to generate Pe without */
     /* doing a matrix triple product.                                   */
     /*------------------------------------------------------------------*/
     /*
     csr_data = (struct ML_CSR_MSRdata *) Pn_coarse->data;
     for (i = 0; i < Tfine->outvec_leng; i++)
     {
       ML_get_matrix_row(Tfine, 1, &i, &allocated, &bindx, &val,
                         &row_length, 0);
       if (row_length == 2)
       {
        agg1 = csr_data->columns[bindx[0]];
        agg2 = csr_data->columns[bindx[1]];
        printf("%d: agg1 and agg2 %d %d   | %d %d\n",i,agg1,
               agg2,bindx[0],bindx[1]);
       
        agg1 = aggr_num(bindx[0], PnMat);
        agg2 = aggr_num(bindx[1], PnMat);
        if (agg1 != agg2) {
           printf("Pe(%d,%d) = something;\n",i,thecolumn);
   
           To get the column suppose we store for each row in Kn_coarse
           the counter above.
           Thus, stored_counter[i] indicates the lowest coarse grid edge that
           is associated with coarse grid node i.
           To get the edge number we compute 
                  min_agg = ML_min(agg1,agg2), 
                  max_agg = ML_max(agg1,agg2);
           The column that we are looking for is given by 
             stored_counter[min_agg] + number of nonzeros in row min_agg of
             Kn_coarse that are greater than min_agg and less than max_agg.
   
   
           'something' is either +1 or -1. To get the sign look at
           Tcoarse(thecolumn,) and Tfine(i,). My guess is that it is related to 
           whether agg1 is greater than agg2 (assuming that val[0] =1)
            }
        }
     }
     */
   
     /********************************************************************/
     /* Matrix triple product for Pe where result is stuffed into MG     */
     /* hierarchy of the edge system.                                    */
     /*------------------------------------------------------------------*/
   
     if (Tfine->invec_leng != ml_nodes->Pmat[grid_level].outvec_leng)
     {
        printf("In ML_Gen_MGHierarchy_UsingReitzinger: Tmat and Pnodal\n"
               "\tdimensions on grid level %d do not agree:\n"
               "\tTmat->invec_leng = %d, Pnodal->outvec_leng = %d\n",
               grid_level,Tfine->invec_leng,
               ml_nodes->Pmat[grid_level].outvec_leng);
        exit(1);
     }
     if (ml_nodes->Pmat[grid_level].invec_leng != Tcoarse_trans->outvec_leng)
     {
        printf("In ML_Gen_MGHierarchy_UsingReitzinger:"
               " Pnodal and Tmat_trans\n"
               "\tdimensions on grid level %d do not agree:\n"
               "\tPnodal->invec_leng = %d, Tcoarse_trans->outvec_leng = %d\n",
               grid_level, ml_nodes->Pmat[grid_level].outvec_leng,
               Tcoarse_trans->outvec_leng);
        exit(1);
     }
   
     ML_rap(Tfine, &(ml_nodes->Pmat[grid_level]), Tcoarse_trans, 
        &(ml_edges->Pmat[grid_level]),ML_CSR_MATRIX);
   
     Pe = &(ml_edges->Pmat[grid_level]);
     csr_data = (struct ML_CSR_MSRdata *) Pe->data;
   
     /********************************************************************/
     /* weed out the 1's and divide the 2's by -2.                       */
     /* Note: my signs seem flipped from what we thought the triple      */
     /* matrix product would do. This could be a function of the Pnmat   */
     /* normalization.                                                   */
     /* Finally, convert the matrix to CSR and strip out the zeros.      */
     /* and make sure that it has the level transfer stuff to be in the  */
     /* MG grid hierarchy.                                               */
     /*------------------------------------------------------------------*/
   
     for (j = 0; j < csr_data->rowptr[Pe->outvec_leng] ; j++)
     {
        if (csr_data->values[j] == 2) csr_data->values[j] = 1;
        else if (csr_data->values[j] == -2) csr_data->values[j] = -1;
        else if (csr_data->values[j] == -1) csr_data->values[j] = 0;
        else if (csr_data->values[j] ==  1) csr_data->values[j] = 0;
        else if (csr_data->values[j] != 0.0)
        {
           printf("ML_Gen_MGHierarchy_UsingReitzinger:"
                  " Error in building Pe.   Found entry %e, expecting"
                  " either +/-1 or -2.\n",csr_data->values[j]);
           fflush(stdout);
        }
     }
    
     /*******************************************************************/
     /* weed out zeros in Pe.                                           */
     /*-----------------------------------------------------------------*/
    
     lower = csr_data->rowptr[0];
     nz_ptr = 0;
     for (i = 0; i < Pe->outvec_leng; i++) {
       for (j = lower; j < csr_data->rowptr[i+1]; j++) {
         if (csr_data->values[j] != 0.0) nz_ptr++;
       }
       lower = csr_data->rowptr[i+1];
       csr_data->rowptr[i+1] = nz_ptr;
     }
     nz_ptr = 0;
     for (i = 0; i < lower; i++) {
       if (csr_data->values[i] != 0.) {
         csr_data->values[nz_ptr] = csr_data->values[i];
         csr_data->columns[nz_ptr] = csr_data->columns[i];
         nz_ptr++;
       }
     }
    
     Pe->getrow->external = CSR_getrows;
     Pe->getrow->internal = NULL;
     Pe->getrow->ML_id    = ML_EXTERNAL;
     Pe->matvec->internal = CSR_matvec;
     Pe->matvec->external = NULL;
     Pe->matvec->ML_id = ML_INTERNAL;
   
/****************** Check the construction of Pe ***********************/
   
#ifdef postprocessscheck
      printf("Checking product Pe * e_i\n");
      yyy = (double *) ML_allocate( Pe->outvec_leng*sizeof(double));
      fido = (double *) ML_allocate( Pe->invec_leng*sizeof(double));
      /*
      printf("%d: Pe->invec_leng = %d\n",Pe->comm->ML_mypid,Pe->invec_leng);
      printf("%d: Tcoarse->outvec_leng = %d\n",Tcoarse->comm->ML_mypid,
             Tcoarse->outvec_leng);
      exit(1);
      */
   
      printf("Pe->invec_leng = %d\n",Pe->invec_leng);
      for (i=0; i< 137; i++)
      {
         for (j=0; j< Pe->invec_leng; j++) fido[j] = 0.;
         if (Pe->comm->ML_nprocs == 1)
            fido[i] = 1;
         else
         {
            /* This is for debugging a 2 process run.  The indices will
               be split between 2 processes;  here, split occurs at 120. */
            if ((Pe->comm->ML_mypid == 0) && (i < 120))
               fido[i] = 1;
            else if (Pe->comm->ML_mypid == 1 && i >= 120)
               fido[i-120] = 1;
         }
   
         ML_Operator_Apply(Pe, Pe->invec_leng, fido,
                           Pe->outvec_leng,yyy);
         dtemp = ML_gdot(Pe->outvec_leng, yyy, yyy, Pe->comm);
         printf("norm(P(:,%d)) = %e\n",i,dtemp); 
      }
   
     fflush(stdout);
     exit(1);
#endif /*postprocesscheck*/

     /* Smooth prolongator. */
     if (smooth_flag == ML_YES)
     {
        if (Tmat->comm->ML_mypid == 0)
           printf("Smoothing edge prolongator...\n");
        ML_Aggregate_Set_Flag_SmoothExistingTentativeP(ag, ML_YES);
        /* default: smooth_factor = 4.0/3.0 */
        if (smooth_factor != ML_DDEFAULT)
           ML_Aggregate_Set_DampingFactor(ag, smooth_factor);
        ML_AGG_Gen_Prolongator(ml_edges,grid_level+1,grid_level,
                               (void *) &(ml_edges->Amat[grid_level+1]), ag);
     }
     if (Tfine->comm->ML_mypid==0 && ag->print_flag < ML_Get_PrintLevel())
     {
        printf("Pe: Total nonzeros = %d (Nrows = %d)\n",
               ml_edges->Pmat[grid_level].N_nonzeros,
               ml_edges->Pmat[grid_level].outvec_leng);
     }
   
     ML_Operator_Set_1Levels(&(ml_edges->Pmat[grid_level]),
                 &(ml_edges->SingleLevel[grid_level]), 
                 &(ml_edges->SingleLevel[grid_level+1]));
     ML_Gen_Restrictor_TransP(ml_edges, grid_level+1, grid_level);
     ML_Gen_AmatrixRAP(ml_edges, grid_level+1, grid_level);

     Tfine = Tcoarse;
  } /* Main FOR loop: for grid_level = fine_level-1 ... */

  ML_free(bindx);
  ML_free(val);

  if (created_ag_obj == 1) ML_Aggregate_Destroy(&ag);
  return(Nlevels_nodal);
}

/******************************************************************************/

int ML_MGHierarchy_ReitzingerDestroy(int finest_level, int coarsest_level,
                     ML_Operator ***Tmat_array, ML_Operator ***Tmat_trans_array)
{
    int level;

    if (*Tmat_array != NULL)
    {
       for (level = finest_level; level >= coarsest_level; level--)
       {
          ML_Operator_Destroy((*Tmat_array)[level]);
          (*Tmat_array)[level] = NULL;
       }
       ML_free(*Tmat_array);
       *Tmat_array = NULL;
    }

    if (*Tmat_trans_array != NULL)
    {
       for (level = finest_level; level >= coarsest_level; level--)
       {
          ML_Operator_Destroy((*Tmat_trans_array)[level]);
          (*Tmat_trans_array)[level] = NULL;
       }
       ML_free(*Tmat_trans_array);
       *Tmat_trans_array = NULL;
    }
    return 0;
}
