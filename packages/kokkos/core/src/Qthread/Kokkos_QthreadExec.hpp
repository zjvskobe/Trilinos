/*
//@HEADER
// ************************************************************************
//
//   Kokkos: Manycore Performance-Portable Multidimensional Arrays
//              Copyright (2012) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact  H. Carter Edwards (hcedwar@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#ifndef KOKKOS_QTHREADEXEC_HPP
#define KOKKOS_QTHREADEXEC_HPP

#include <impl/Kokkos_spinwait.hpp>

//----------------------------------------------------------------------------

namespace Kokkos {
namespace Impl {

//----------------------------------------------------------------------------

class QthreadExec ;

typedef void (*QthreadExecFunctionPointer)( QthreadExec & , const void * );

class QthreadExec {
private:

  enum { Inactive = 0 , Active = 1 };

  const QthreadExec * const * m_worker_base ;
  const QthreadExec * const * m_shepherd_base ;

  void  * m_scratch_alloc ;  ///< Scratch memory [ reduce , team , shared ]
  int     m_reduce_end ;     ///< End of scratch reduction memory

  int     m_shepherd_rank ;
  int     m_shepherd_size ;

  int     m_shepherd_worker_rank ;
  int     m_shepherd_worker_size ;

  /*
   *  m_worker_rank = m_shepherd_rank * m_shepherd_worker_size + m_shepherd_worker_rank
   *  m_worker_size = m_shepherd_size * m_shepherd_worker_size
   */
  int     m_worker_rank ;
  int     m_worker_size ;

  int mutable volatile m_worker_state ;


  friend class Kokkos::Qthread ;

  ~QthreadExec();
  QthreadExec( const QthreadExec & );
  QthreadExec & operator = ( const QthreadExec & );

public:

  QthreadExec();

  /** Execute the input function on all available Qthread workers */
  static void exec_all( Qthread & , QthreadExecFunctionPointer , const void * );

  //----------------------------------------
  /** Barrier across all workers participating in the 'exec_all' */
  void exec_all_barrier() const
    {
      const int rev_rank = m_worker_size - ( m_worker_rank + 1 );

      int n , j ;

      for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < m_worker_size ) ; n <<= 1 ) {
        Impl::spinwait( m_worker_base[j]->m_worker_state , QthreadExec::Active );
      }

      if ( rev_rank ) {
        m_worker_state = QthreadExec::Inactive ;
        Impl::spinwait( m_worker_state , QthreadExec::Inactive );
      }
    
      for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < m_worker_size ) ; n <<= 1 ) {
        m_worker_base[j]->m_worker_state = QthreadExec::Active ;
      }
    }

  /** Barrier across workers within the shepherd with rank < team_rank */
  void shepherd_barrier( const int team_size ) const
    {
      if ( m_shepherd_worker_rank < team_size ) {

        const int rev_rank = team_size - ( m_shepherd_worker_rank + 1 );

        int n , j ;

        for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < team_size ) ; n <<= 1 ) {
          Impl::spinwait( m_shepherd_base[j]->m_worker_state , QthreadExec::Active );
        }

        if ( rev_rank ) {
          m_worker_state = QthreadExec::Inactive ;
          Impl::spinwait( m_worker_state , QthreadExec::Inactive );
        }
    
        for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < team_size ) ; n <<= 1 ) {
          m_shepherd_base[j]->m_worker_state = QthreadExec::Active ;
        }
      }
    }

  //----------------------------------------
  /** Reduce across all workers participating in the 'exec_all' */
  template< class FunctorType >
  inline
  void exec_all_reduce( const FunctorType & func ) const
    {
      typedef ReduceAdapter< FunctorType >  Reduce ;

      const int rev_rank = m_worker_size - ( m_worker_rank + 1 );

      int n , j ;

      for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < m_worker_size ) ; n <<= 1 ) {
        const QthreadExec & fan = *m_worker_base[j];

        Impl::spinwait( fan.m_worker_state , QthreadExec::Active );

        Reduce::join( func , m_scratch_alloc , fan.m_scratch_alloc );
      }

      if ( rev_rank ) {
        m_worker_state = QthreadExec::Inactive ;
        Impl::spinwait( m_worker_state , QthreadExec::Inactive );
      }
    
      for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < m_worker_size ) ; n <<= 1 ) {
        m_worker_base[j]->m_worker_state = QthreadExec::Active ;
      }
    }

  //----------------------------------------
  /** Scall across all workers participating in the 'exec_all' */
  template< class FunctorType >
  inline
  void exec_all_scan( const FunctorType & func ) const
    {
      typedef ReduceAdapter< FunctorType >  Reduce ;

      const int rev_rank = m_worker_size - ( m_worker_rank + 1 );

      int n , j ;

      for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < m_worker_size ) ; n <<= 1 ) {
        Impl::spinwait( m_worker_base[j]->m_worker_state , QthreadExec::Active );
      }

      if ( rev_rank ) {
        m_worker_state = QthreadExec::Inactive ;
        Impl::spinwait( m_worker_state , QthreadExec::Inactive );
      }
      else {
        // Root thread scans across values before releasing threads
        // Worker data is in reverse order, so m_worker_base[0] is the 
        // highest ranking thread.

        // Copy from lower ranking to higher ranking worker.
        for ( int i = 1 ; i < n ; ++i ) {
          Reduce::copy( func , m_worker_base[i-1]->m_scratch_alloc
                             , m_worker_base[i]->m_scratch_alloc );
        }

        Reduce::init( func , m_worker_base[n-1]->m_scratch_alloc );

        // Join from lower ranking to higher ranking worker.
        // Value at m_worker_base[n-1] is zero so skip adding it to m_worker_base[n-2].
        for ( int i = n - 1 ; --i ; ) {
          Reduce::join( func , m_worker_base[i-1]->m_scratch_alloc
                             , m_worker_base[i]->m_scratch_alloc );
        }
      }
    
      for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < m_worker_size ) ; n <<= 1 ) {
        m_worker_base[j]->m_worker_state = QthreadExec::Active ;
      }
    }

  //----------------------------------------

  template< class Type>
  inline
  volatile Type * shepherd_team_scratch_value() const
    { return (volatile Type*)(((unsigned char *) m_scratch_alloc) + m_reduce_end); }

  template< class Type >
  inline
  Type shepherd_reduce( const int team_size , const Type & value ) const
    {
      *shepherd_team_scratch_value<Type>() = value ;

      memory_fence();

      const int rev_rank = team_size - ( m_shepherd_worker_rank + 1 );

      int n , j ;

      for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < team_size ) ; n <<= 1 ) {
        Impl::spinwait( m_shepherd_base[j]->m_worker_state , QthreadExec::Active );
      }

      if ( rev_rank ) {
        m_worker_state = QthreadExec::Inactive ;
        Impl::spinwait( m_worker_state , QthreadExec::Inactive );
      }
      else {
        Type & accum = * m_shepherd_base[0]->shepherd_team_scratch_value<Type>();
        for ( int i = 1 ; i < n ; ++i ) {
          accum += * m_shepherd_base[i]->shepherd_team_scratch_value<Type>();
        }
        for ( int i = 1 ; i < n ; ++i ) {
          * m_shepherd_base[i]->shepherd_team_scratch_value<Type>() = accum ;
        }

        memory_fence();
      }

      for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < team_size ) ; n <<= 1 ) {
        m_shepherd_base[j]->m_worker_state = QthreadExec::Active ;
      }

      return *shepherd_team_scratch_value<Type>();
    }

  template< class JoinOp >
  inline
  typename JoinOp::value_type
    shepherd_reduce( const int team_size
                   , const typename JoinOp::value_type & value
                   , const JoinOp & op ) const
    {
      typedef typename JoinOp::value_type Type ;

      *shepherd_team_scratch_value<Type>() = value ;

      memory_fence();

      const int rev_rank = team_size - ( m_shepherd_worker_rank + 1 );

      int n , j ;

      for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < team_size ) ; n <<= 1 ) {
        Impl::spinwait( m_shepherd_base[j]->m_worker_state , QthreadExec::Active );
      }

      if ( rev_rank ) {
        m_worker_state = QthreadExec::Inactive ;
        Impl::spinwait( m_worker_state , QthreadExec::Inactive );
      }
      else {
        volatile Type & accum = * m_shepherd_base[0]->shepherd_team_scratch_value<Type>();
        for ( int i = 1 ; i < n ; ++i ) {
          op.join( accum , * m_shepherd_base[i]->shepherd_team_scratch_value<Type>() );
        }
        for ( int i = 1 ; i < n ; ++i ) {
          * m_shepherd_base[i]->shepherd_team_scratch_value<Type>() = accum ;
        }

        memory_fence();
      }

      for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < team_size ) ; n <<= 1 ) {
        m_shepherd_base[j]->m_worker_state = QthreadExec::Active ;
      }

      return *shepherd_team_scratch_value<Type>();
    }

  template< class Type >
  inline
  Type shepherd_scan( const int team_size
                    , const Type & value
                    ,       Type * const global_value = 0 ) const
    {
      *shepherd_team_scratch_value<Type>() = value ;

      memory_fence();

      const int rev_rank = team_size - ( m_shepherd_worker_rank + 1 );

      int n , j ;

      for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < team_size ) ; n <<= 1 ) {
        Impl::spinwait( m_shepherd_base[j]->m_worker_state , QthreadExec::Active );
      }

      if ( rev_rank ) {
        m_worker_state = QthreadExec::Inactive ;
        Impl::spinwait( m_worker_state , QthreadExec::Inactive );
      }
      else {
        // Root thread scans across values before releasing threads
        // Worker data is in reverse order, so m_shepherd_base[0] is the 
        // highest ranking thread.

        // Copy from lower ranking to higher ranking worker.

        Type accum = * m_shepherd_base[0]->shepherd_team_scratch_value<Type>();
        for ( int i = 1 ; i < n ; ++i ) {
          const Type tmp = * m_shepherd_base[i]->shepherd_team_scratch_value<Type>();
          accum += tmp ;
          * m_shepherd_base[i-1]->shepherd_team_scratch_value<Type>() = tmp ;
        }

        * m_shepherd_base[n-1]->shepherd_team_scratch_value<Type>() =
          global_value ? atomic_fetch_add( global_value , accum ) : 0 ;

        // Join from lower ranking to higher ranking worker.
        for ( int i = n ; --i ; ) {
          * m_shepherd_base[i-1]->shepherd_team_scratch_value<Type>() += * m_shepherd_base[i]->shepherd_team_scratch_value<Type>();
        }

        memory_fence();
      }
    
      for ( n = 1 ; ( ! ( rev_rank & n ) ) && ( ( j = rev_rank + n ) < team_size ) ; n <<= 1 ) {
        m_shepherd_base[j]->m_worker_state = QthreadExec::Active ;
      }

      return *shepherd_team_scratch_value<Type>();
    }

  //----------------------------------------

  static inline
  int align_alloc( int size )
    {
      enum { ALLOC_GRAIN = 1 << 6 /* power of two, 64bytes */};
      enum { ALLOC_GRAIN_MASK = ALLOC_GRAIN - 1 };
      return ( size + ALLOC_GRAIN_MASK ) & ~ALLOC_GRAIN_MASK ;
    }

  void shared_reset( Qthread::scratch_memory_space & );

  void * exec_all_reduce_value() const { return m_scratch_alloc ; }

  static void * exec_all_reduce_result();

  static void resize_worker_scratch( const int reduce_size , const int shared_size );
  static void clear_workers();

  //----------------------------------------

  inline int worker_rank() const { return m_worker_rank ; }
  inline int worker_size() const { return m_worker_size ; }
  inline int shepherd_worker_rank() const { return m_shepherd_worker_rank ; }
  inline int shepherd_worker_size() const { return m_shepherd_worker_size ; }
  inline int shepherd_rank() const { return m_shepherd_rank ; }
  inline int shepherd_size() const { return m_shepherd_size ; }
};

} /* namespace Impl */
} /* namespace Kokkos */

//----------------------------------------------------------------------------

namespace Kokkos {
namespace Impl {

class QthreadTeamPolicyMember {
private:

  typedef Kokkos::Qthread                        execution_space ;
  typedef execution_space::scratch_memory_space  scratch_memory_space ;


        Impl::QthreadExec   & m_exec ;
  scratch_memory_space        m_team_shared ;
  const int                   m_team_size ;
  const int                   m_team_rank ;
  const int                   m_league_size ;
  const int                   m_league_end ;
        int                   m_league_rank ;

public:

  KOKKOS_INLINE_FUNCTION
  const scratch_memory_space & team_shmem() const { return m_team_shared ; }

  KOKKOS_INLINE_FUNCTION int league_rank() const { return m_league_rank ; }
  KOKKOS_INLINE_FUNCTION int league_size() const { return m_league_size ; }
  KOKKOS_INLINE_FUNCTION int team_rank() const { return m_team_rank ; }
  KOKKOS_INLINE_FUNCTION int team_size() const { return m_team_size ; }

  KOKKOS_INLINE_FUNCTION void team_barrier() const
#if ! defined( KOKKOS_ACTIVE_EXECUTION_MEMORY_SPACE_HOST )
    {}
#else
    { m_exec.shepherd_barrier( m_team_size ); }
#endif

  template< typename Type >
  KOKKOS_INLINE_FUNCTION Type team_reduce( const Type & value ) const
#if ! defined( KOKKOS_ACTIVE_EXECUTION_MEMORY_SPACE_HOST )
    { return Type(); }
#else
    { return m_exec.template shepherd_reduce<Type>( m_team_size , value ); }
#endif

  template< typename JoinOp >
  KOKKOS_INLINE_FUNCTION typename JoinOp::value_type
    team_reduce( const typename JoinOp::value_type & value
               , const JoinOp & op ) const
#if ! defined( KOKKOS_ACTIVE_EXECUTION_MEMORY_SPACE_HOST )
    { return typename JoinOp::value_type(); }
#else
    { return m_exec.template shepherd_reduce<JoinOp>( m_team_size , value , op ); }
#endif

  /** \brief  Intra-team exclusive prefix sum with team_rank() ordering.
   *
   *  The highest rank thread can compute the reduction total as
   *    reduction_total = dev.team_scan( value ) + value ;
   */
  template< typename Type >
  KOKKOS_INLINE_FUNCTION Type team_scan( const Type & value ) const
#if ! defined( KOKKOS_ACTIVE_EXECUTION_MEMORY_SPACE_HOST )
    { return Type(); }
#else
    { return m_exec.template shepherd_scan<Type>( m_team_size , value ); }
#endif

  /** \brief  Intra-team exclusive prefix sum with team_rank() ordering
   *          with intra-team non-deterministic ordering accumulation.
   *
   *  The global inter-team accumulation value will, at the end of the
   *  league's parallel execution, be the scan's total.
   *  Parallel execution ordering of the league's teams is non-deterministic.
   *  As such the base value for each team's scan operation is similarly
   *  non-deterministic.
   */
  template< typename Type >
  KOKKOS_INLINE_FUNCTION Type team_scan( const Type & value , Type * const global_accum ) const
#if ! defined( KOKKOS_ACTIVE_EXECUTION_MEMORY_SPACE_HOST )
    { return Type(); }
#else
    { return m_exec.template shepherd_scan<Type>( m_team_size , value , global_accum ); }
#endif

  //----------------------------------------
  // Private for the driver ( for ( member_type i(exec,team); i ; i.next_team() ) { ... }

  // Initialize
  template< class Arg0 , class Arg1 >
  QthreadTeamPolicyMember( Impl::QthreadExec & exec , const TeamPolicy<Arg0,Arg1,Qthread> & team )
    : m_exec( exec )
    , m_team_shared(0,0)
    , m_team_size(   team.m_team_size )
    , m_team_rank(   exec.shepherd_worker_rank() )
    , m_league_size( team.m_league_size )
    , m_league_end(  team.m_league_size - team.m_shepherd_iter * ( exec.shepherd_size() - ( exec.shepherd_rank() + 1 ) ) )
    , m_league_rank( m_league_end > team.m_shepherd_iter ? m_league_end - team.m_shepherd_iter : 0 )
  {
    m_exec.shared_reset( m_team_shared );
  }

  // Continue
  operator bool () const { return m_league_rank < m_league_end ; }

  // iterate
  void next_team() { ++m_league_rank ; m_exec.shared_reset( m_team_shared ); }
};

} // namespace Impl

template< class Arg0 , class Arg1 >
class TeamPolicy< Arg0 , Arg1 , Kokkos::Qthread >
{
private:

  const int m_league_size ;
  const int m_team_size ;
  const int m_shepherd_iter ;

public:

  //! Tag this class as a kokkos execution policy
  typedef TeamPolicy  execution_policy ;
  typedef Qthread     execution_space ;

  typedef typename
    Impl::if_c< ! Impl::is_same< Kokkos::Qthread , Arg0 >::value , Arg0 , Arg1 >::type
      work_tag ;

  inline int team_size()   const { return m_team_size ; }
  inline int league_size() const { return m_league_size ; }

  // One active team per shepherd
  TeamPolicy( Kokkos::Qthread & q
            , const int league_size
            , const int team_size
            )
    : m_league_size( league_size )
    , m_team_size( team_size < q.shepherd_worker_size()
                 ? team_size : q.shepherd_worker_size() )
    , m_shepherd_iter( ( league_size + q.shepherd_size() - 1 ) / q.shepherd_size() )
    {
    }

  // One active team per shepherd
  TeamPolicy( const int league_size
            , const int team_size
            )
    : m_league_size( league_size )
    , m_team_size( team_size < Qthread::instance().shepherd_worker_size()
                 ? team_size : Qthread::instance().shepherd_worker_size() )
    , m_shepherd_iter( ( league_size + Qthread::instance().shepherd_size() - 1 ) / Qthread::instance().shepherd_size() )
    {
    }

  template< class FunctorType >
  inline static
  int team_size_max( const FunctorType & )
    { return Qthread::instance().shepherd_worker_size(); }

  typedef Impl::QthreadTeamPolicyMember member_type ;

  friend class Impl::QthreadTeamPolicyMember ;
};

template< unsigned VectorLength
        , class Arg0
        , class Arg1 >
class TeamVectorPolicy<VectorLength, Arg0, Arg1, Kokkos::Qthread> {
public:
  //! Tag this class as a kokkos execution policy
  typedef TeamVectorPolicy  execution_policy ;
  typedef Kokkos::Qthread   execution_space ;

  typedef typename
    Impl::if_c< ! Impl::is_same< Kokkos::Qthread , Arg0 >::value , Arg0 , Arg1 >::type
      work_tag ;
private:
  const int m_league_size ;
  const int m_team_size ;
  const int m_shepherd_iter ;

public:

  // One active team per shepherd
  TeamVectorPolicy( Kokkos::Qthread & q
            , const int league_size
            , const int team_size
            )
    : m_league_size( league_size )
    , m_team_size( team_size < q.shepherd_worker_size()
                 ? team_size : q.shepherd_worker_size() )
    , m_shepherd_iter( ( league_size + q.shepherd_size() - 1 ) / q.shepherd_size() )
    {
    }

  // One active team per shepherd
  TeamVectorPolicy( const int league_size
            , const int team_size
            )
    : m_league_size( league_size )
    , m_team_size( team_size < Qthread::instance().shepherd_worker_size()
                 ? team_size : Qthread::instance().shepherd_worker_size() )
    , m_shepherd_iter( ( league_size + Qthread::instance().shepherd_size() - 1 ) / Qthread::instance().shepherd_size() )
    {
    }

  template< class FunctorType >
  inline static
  int team_size_max( const FunctorType & )
    { return Qthread::instance().shepherd_worker_size(); }

  class member_type {
  private:
    typedef Kokkos::Qthread::scratch_memory_space scratch_memory_space ;

          Impl::QthreadExec   & m_exec ;
    scratch_memory_space        m_team_shared ;
    const int                   m_team_size ;
    const int                   m_team_rank ;
    const int                   m_league_size ;
    const int                   m_league_end ;
          int                   m_league_rank ;

  public:

    KOKKOS_INLINE_FUNCTION
    const scratch_memory_space & team_shmem() const { return m_team_shared ; }

    KOKKOS_INLINE_FUNCTION int league_rank() const { return m_league_rank ; }
    KOKKOS_INLINE_FUNCTION int league_size() const { return m_league_size ; }
    KOKKOS_INLINE_FUNCTION int team_rank() const { return m_team_rank ; }
    KOKKOS_INLINE_FUNCTION int team_size() const { return m_team_size ; }

    KOKKOS_INLINE_FUNCTION void team_barrier() const
      { m_exec.shepherd_barrier( m_team_size ); }

    /** \brief  Intra-team exclusive prefix sum with team_rank() ordering.
     *
     *  The highest rank thread can compute the reduction total as
     *    reduction_total = dev.team_scan( value ) + value ;
     */
    template< typename Type >
    KOKKOS_INLINE_FUNCTION Type team_scan( const Type & value ) const
      { return m_exec.template shepherd_scan<Type>( m_team_size , value ); }

    /** \brief  Intra-team exclusive prefix sum with team_rank() ordering
     *          with intra-team non-deterministic ordering accumulation.
     *
     *  The global inter-team accumulation value will, at the end of the
     *  league's parallel execution, be the scan's total.
     *  Parallel execution ordering of the league's teams is non-deterministic.
     *  As such the base value for each team's scan operation is similarly
     *  non-deterministic.
     */
    template< typename Type >
    KOKKOS_INLINE_FUNCTION Type team_scan( const Type & value , Type * const global_accum ) const
      { return m_exec.template shepherd_scan<Type>( m_team_size , value , global_accum ); }

    //----------------------------------------
    // Private for the driver ( for ( member_type i(exec,team); i ; i.next_team() ) { ... }

    // Initialize
    member_type( Impl::QthreadExec & exec , const TeamVectorPolicy & team )
      : m_exec( exec )
      , m_team_shared(0,0)
      , m_team_size(   team.m_team_size )
      , m_team_rank(   exec.shepherd_worker_rank() )
      , m_league_size( team.m_league_size )
      , m_league_end(  team.m_league_size - team.m_shepherd_iter * ( exec.shepherd_size() - ( exec.shepherd_rank() + 1 ) ) )
      , m_league_rank( m_league_end > team.m_shepherd_iter ? m_league_end - team.m_shepherd_iter : 0 )
    { m_exec.shared_reset( m_team_shared ); }

    // Continue
    operator bool () const { return m_league_rank < m_league_end ; }

    // iterate
    void next_team() { ++m_league_rank ; m_exec.shared_reset( m_team_shared ); }

#ifdef KOKKOS_HAVE_CXX11

  /** \brief  Guarantees execution of op() with only a single vector lane of this thread. */
  template< class Operation >
  KOKKOS_INLINE_FUNCTION void vector_single(const Operation & op) const {
    op();
  }

  /** \brief  Guarantees execution of op() with only a single vector lane of this thread. */
  template< class Operation , typename ValueType>
  KOKKOS_INLINE_FUNCTION void vector_single(const Operation & op, ValueType& bcast) const {
    op();
  }
  /** \brief  Intra-thread vector parallel for. Executes op(iType i) for each i=0..N-1.
   *
   * The range i=0..N-1 is mapped to all vector lanes of the the calling thread.
   * This functionality requires C++11 support.*/
  template< typename iType, class Operation >
  KOKKOS_INLINE_FUNCTION void vector_par_for(const iType n, const Operation & op) const {
    #ifdef KOKKOS_HAVE_PRAGMA_IVDEP
    #pragma ivdep
    #endif
    for(int i=0; i<n ; i++) {
      op(i);
    }
  }

  /** \brief  Intra-thread vector parallel reduce. Executes op(iType i, ValueType & val) for each i=0..N-1.
   *
   * The range i=0..N-1 is mapped to all vector lanes of the the calling thread and a summation of
   * val is performed and put into result. This functionality requires C++11 support.*/
  template< typename iType, class Operation, typename ValueType >
  KOKKOS_INLINE_FUNCTION void vector_par_reduce(const iType n, const Operation & op, ValueType& result) const {

    result = ValueType();

#ifdef KOKKOS_HAVE_PRAGMA_IVDEP
#pragma ivdep
#endif
    for(int i=0; i<n ; i++) {
      ValueType tmp = ValueType();
      op(i,tmp);
      result+=tmp;
    }
  }

  /** \brief  Intra-thread vector parallel reduce. Executes op(iType i, ValueType & val) for each i=0..N-1.
   *
   * The range i=0..N-1 is mapped to all vector lanes of the the calling thread and a reduction of
   * val is performed using JoinType(ValueType& val, const ValueType& update) and put into init_result.
   * The input value of init_result is used as initializer for temporary variables of ValueType. Therefore
   * the input value should be the neutral element with respect to the join operation (e.g. '0 for +-' or
   * '1 for *'). This functionality requires C++11 support.*/
  template< typename iType, class Operation, typename ValueType, class JoinType >
  KOKKOS_INLINE_FUNCTION void vector_par_reduce(const iType n, const Operation & op, ValueType& init_result, const JoinType & join) const {

    ValueType result = init_result;

#ifdef KOKKOS_HAVE_PRAGMA_IVDEP
#pragma ivdep
#endif
    for(int i=0; i<n ; i++) {
      ValueType tmp = init_result;
      op(i,tmp);
      join(result,tmp);
    }
    init_result = result;
  }


  /** \brief  Intra-thread vector parallel exclusive prefix sum. Executes op(iType i, ValueType & val, bool final)
   *          for each i=0..N-1.
   *
   * The range i=0..N-1 is mapped to all vector lanes in the thread and a scan operation is performed.
   * Depending on the target execution space the operator might be called twice: once with final=false
   * and once with final=true. When final==true val contains the prefix sum value. The contribution of this
   * "i" needs to be added to val no matter whether final==true or not. In a serial execution
   * (i.e. team_size==1) the operator is only called once with final==true. Scan_val will be set
   * to the final sum value over all vector lanes.
   * This functionality requires C++11 support.*/
  template< typename iType, class Operation, typename ValueType >
  KOKKOS_INLINE_FUNCTION  void vector_par_scan(const iType n, const Operation & op, ValueType& scan_val) const {

    scan_val = ValueType();

#ifdef KOKKOS_HAVE_PRAGMA_IVDEP
#pragma ivdep
#endif
    for(int i=0; i<n ; i++) {
      op(i,scan_val,true);
    }
  }
#endif
  };
};

} /* namespace Kokkos */

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

#endif /* #define KOKKOS_QTHREADEXEC_HPP */

