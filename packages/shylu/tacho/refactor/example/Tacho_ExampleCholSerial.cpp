#include "Teuchos_CommandLineProcessor.hpp"

#include "ShyLUTacho_config.h"

#include <Kokkos_Core.hpp>
#include <impl/Kokkos_Timer.hpp>

#include "TachoExp_Util.hpp"
#include "TachoExp_CrsMatrixBase.hpp"
#include "TachoExp_MatrixMarket.hpp"

#include "TachoExp_Graph.hpp"
#include "TachoExp_SymbolicTools.hpp"

#if defined(HAVE_SHYLUTACHO_SCOTCH)
#include "TachoExp_GraphTools_Scotch.hpp"
#endif

#if defined(HAVE_SHYLUTACHO_METIS)
#include "TachoExp_GraphTools_Metis.hpp"
#endif

#include "TachoExp_GraphTools_CAMD.hpp"

#include "TachoExp_NumericTools.hpp"

#ifdef HAVE_SHYLUTACHO_MKL
#include "mkl_service.h"
#endif

using namespace Tacho;
using namespace Tacho::Experimental;

int main (int argc, char *argv[]) {
  Teuchos::CommandLineProcessor clp;
  clp.setDocString("This example program measure the performance of Pardiso Chol algorithms on Kokkos::OpenMP execution space.\n");

  int nthreads = 1;
  clp.setOption("kokkos-threads", &nthreads, "Number of threads");

  bool verbose = false;
  clp.setOption("enable-verbose", "disable-verbose", &verbose, "Flag for verbose printing");

  std::string file_input = "test.mtx";
  clp.setOption("file-input", &file_input, "Input file (MatrixMarket SPD matrix)");

  int nrhs = 1;
  clp.setOption("nrhs", &nrhs, "Number of RHS vectors");

  clp.recogniseAllOptions(true);
  clp.throwExceptions(false);

  Teuchos::CommandLineProcessor::EParseCommandLineReturn r_parse= clp.parse( argc, argv );

  if (r_parse == Teuchos::CommandLineProcessor::PARSE_HELP_PRINTED) return 0;
  //if (r_parse != Teuchos::CommandLineProcessor::PARSE_SUCCESSFUL  ) return -1;

  Kokkos::initialize(argc, argv);
  Kokkos::DefaultHostExecutionSpace::print_configuration(std::cout, false);

  int r_val = 0;

  {
    typedef double value_type;
    typedef CrsMatrixBase<value_type> CrsMatrixBaseType;
    typedef Kokkos::View<value_type**,Kokkos::LayoutLeft,Kokkos::DefaultHostExecutionSpace> DenseMatrixBaseType;
    
    Kokkos::Impl::Timer timer;
    double t = 0.0;

    std::cout << "CholSerial:: import input file = " << file_input << std::endl;
    CrsMatrixBaseType A("A");
    timer.reset();
    {
      {
        std::ifstream in;
        in.open(file_input);
        if (!in.good()) {
          std::cout << "Failed in open the file: " << file_input << std::endl;
          return -1;
        }
      }
      A = MatrixMarket<value_type>::read(file_input);
    }
    Graph G(A);
    t = timer.seconds();
    std::cout << "CholSerial:: import input file::time = " << t << std::endl;
    
    DenseMatrixBaseType 
      BB("BB", A.NumRows(), nrhs), 
      XX("XX", A.NumRows(), nrhs), 
      RR("RR", A.NumRows(), nrhs),
      PP("PP",  A.NumRows(), 1);
    
    {
      const auto m = A.NumRows();
      srand(time(NULL));
      for (ordinal_type rhs=0;rhs<nrhs;++rhs) {
        for (ordinal_type i=0;i<m;++i) 
          XX(i, rhs) = ((value_type)rand()/(RAND_MAX));
        
        // matvec
        Kokkos::DefaultHostExecutionSpace::fence();
        Kokkos::parallel_for(Kokkos::RangePolicy<Kokkos::DefaultHostExecutionSpace>(0, m),
                             [&](const ordinal_type i) {
                               value_type tmp = 0;
                               for (ordinal_type j=A.RowPtrBegin(i);j<A.RowPtrEnd(i);++j)
                                 tmp += A.Value(j)*XX(A.Col(j), rhs);
                               BB(i, rhs) = tmp;
                             } );
        Kokkos::DefaultHostExecutionSpace::fence();
      }
      Kokkos::deep_copy(RR, XX);
    }

    std::cout << "CholSerial:: analyze matrix" << std::endl;
    timer.reset();
#if   defined(HAVE_SHYLUTACHO_METIS)
    GraphTools_Metis T(G);
#elif defined(HAVE_SHYLUTACHO_SCOTCH)
    GraphTools_Scotch T(G);
#else
    GraphTools_CAMD T(G);
#endif
    T.reorder();
    
    SymbolicTools S(A, T);
    S.symbolicFactorize();
    t = timer.seconds();
    std::cout << "CholSerial:: analyze matrix::time = " << t << std::endl;
    
    std::cout << "CholSerial:: factorize matrix" << std::endl;
    timer.reset();    
    NumericTools<value_type,Kokkos::DefaultHostExecutionSpace> 
      N(A.NumRows(), A.RowPtr(), A.Cols(), A.Values(),
        T.PermVector(), T.InvPermVector(),
        S.NumSuperNodes(), S.SuperNodes(),
        S.gidSuperPanelPtr(), S.gidSuperPanelColIdx(),
        S.sidSuperPanelPtr(), S.sidSuperPanelColIdx(), S.blkSuperPanelColIdx(),
        S.SuperNodesTreePtr(), S.SuperNodesTreeChildren(), S.SuperNodesTreeRoots());
    
    N.CholeskyFactorize();
    t = timer.seconds();

    // {
    //   std::ofstream out("U.mtx");
    //   auto U = N.Factors<Uplo::Upper>();
    //   MatrixMarket<value_type>::write(out, U);
    // }
    std::cout << "CholSerial:: factorize matrix::time = " << t << std::endl;

    
  }

  Kokkos::finalize();

  return r_val;
}