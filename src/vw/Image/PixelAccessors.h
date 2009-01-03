// __BEGIN_LICENSE__
// 
// Copyright (C) 2006 United States Government as represented by the
// Administrator of the National Aeronautics and Space Administration
// (NASA).  All Rights Reserved.
// 
// Copyright 2006 Carnegie Mellon University. All rights reserved.
// 
// This software is distributed under the NASA Open Source Agreement
// (NOSA), version 1.3.  The NOSA has been approved by the Open Source
// Initiative.  See the file COPYING at the top of the distribution
// directory tree for the complete NOSA document.
// 
// THE SUBJECT SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY OF ANY
// KIND, EITHER EXPRESSED, IMPLIED, OR STATUTORY, INCLUDING, BUT NOT
// LIMITED TO, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL CONFORM TO
// SPECIFICATIONS, ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR
// A PARTICULAR PURPOSE, OR FREEDOM FROM INFRINGEMENT, ANY WARRANTY THAT
// THE SUBJECT SOFTWARE WILL BE ERROR FREE, OR ANY WARRANTY THAT
// DOCUMENTATION, IF PROVIDED, WILL CONFORM TO THE SUBJECT SOFTWARE.
// 
// __END_LICENSE__

/// \file PixelAccessors.h
/// 
/// Standard pixel accessor types for general image views.
/// 
#ifndef __VW_IMAGE_PIXELACCESSORS_H__
#define __VW_IMAGE_PIXELACCESSORS_H__

#include <boost/type_traits.hpp>
#include <boost/mpl/if.hpp>

#include <vw/Core/FundamentalTypes.h>

namespace vw {

  // Forward-declaration.
  template <class ViewT> struct IsFloatingPointIndexable;

  /// This type computation routine is handy when you are writing a
  /// functor and you need a template class that returns a
  /// PixelAccessor's pixel_type as the ::type keyword.  This is handy
  /// when writing functors for a PerPixelAccessorView, for example.
  template <class PixelAccessorT> 
  struct PixelTypeFromPixelAccessor {
    typedef typename PixelAccessorT::pixel_type type;
  };

  /// A memory striding pixel accessor for traversing an image stored
  /// in memory.
  ///
  /// A pixel accessor for image data stored in the usual manner in main 
  /// memory, moving between pixels using specified i and j strides.
  /// This template produces the correct const accessor when passed a 
  /// const pixel type.  It is primarily intended to be used by ImageView.
  template <class PixelT>
  class MemoryStridingPixelAccessor {
#if defined(VW_ENABLE_BOUNDS_CHECK) && (VW_ENABLE_BOUNDS_CHECK==1)
    PixelT *m_base_ptr; 
    int32 m_num_pixels;
#endif
    PixelT *m_ptr;
    ptrdiff_t m_cstride, m_rstride, m_pstride;
  public:
    typedef PixelT pixel_type;
    typedef PixelT& result_type;

#if defined(VW_ENABLE_BOUNDS_CHECK) && (VW_ENABLE_BOUNDS_CHECK==1)
    MemoryStridingPixelAccessor( PixelT *ptr, 
                                 ptrdiff_t cstride, ptrdiff_t rstride, ptrdiff_t pstride,
                                 int32 cols, int32 rows, int32 planes)
      : m_base_ptr(ptr), m_num_pixels(cols * rows * planes),
        m_ptr(ptr), m_cstride(cstride), m_rstride(rstride), m_pstride(pstride) {}
#else
    MemoryStridingPixelAccessor( PixelT *ptr, ptrdiff_t cstride, ptrdiff_t rstride, ptrdiff_t pstride )
      : m_ptr(ptr), m_cstride(cstride), m_rstride(rstride), m_pstride(pstride) {
    }
#endif
    
    inline MemoryStridingPixelAccessor& next_col() { m_ptr += m_cstride; return *this; }
    inline MemoryStridingPixelAccessor& prev_col() { m_ptr -= m_cstride; return *this; }
    inline MemoryStridingPixelAccessor& next_row() { m_ptr += m_rstride; return *this; }
    inline MemoryStridingPixelAccessor& prev_row() { m_ptr -= m_rstride; return *this; }
    inline MemoryStridingPixelAccessor& next_plane() { m_ptr += m_pstride; return *this; }
    inline MemoryStridingPixelAccessor& prev_plane() { m_ptr -= m_pstride; return *this; }
    inline MemoryStridingPixelAccessor& advance( ptrdiff_t dc, ptrdiff_t dr, ptrdiff_t dp=0 ) {
      m_ptr += dc*m_cstride + dr*m_rstride + dp*m_pstride;
      return *this;
    }

    inline result_type operator*() const { 
#if defined(VW_ENABLE_BOUNDS_CHECK) && (VW_ENABLE_BOUNDS_CHECK==1)
      int32 delta = int32(m_ptr - m_base_ptr);
      if (delta < 0 || delta >= m_num_pixels)
        vw_throw(ArgumentErr() << "MemoryStridingPixelAccessor() - invalid index " << delta << " / " << (m_num_pixels-1) << ".");
#endif
      return *m_ptr; 
    }
  };


  /// A generic "procedural" pixel accessor that keeps track of it
  /// position (c,r,p) in the image.
  ///
  /// A pixel accessor for views that are procedurally-generated, 
  /// and thus can't actually be pointed to.  This accessor simply 
  /// keeps track of the current position in image coordintes and 
  /// invokes the view's function operator when dereferenced.
  template <class ViewT>
  class ProceduralPixelAccessor {
    typedef typename boost::mpl::if_<IsFloatingPointIndexable<ViewT>, double, int32>::type offset_type;
    ViewT const& m_view;
    offset_type m_c, m_r;
    int32 m_p;
  public:
    typedef typename ViewT::pixel_type pixel_type;
    typedef typename ViewT::result_type result_type;

    ProceduralPixelAccessor( ViewT const& view ) : m_view(view), m_c(), m_r(), m_p() {}
    ProceduralPixelAccessor( ViewT const& view, offset_type c, offset_type r, int32 p=0 ) : m_view(view), m_c(c), m_r(r), m_p(p) {}

    inline ProceduralPixelAccessor& next_col() { ++m_c; return *this; }
    inline ProceduralPixelAccessor& prev_col() { --m_c; return *this; }
    inline ProceduralPixelAccessor& next_row() { ++m_r; return *this; }
    inline ProceduralPixelAccessor& prev_row() { --m_r; return *this; }
    inline ProceduralPixelAccessor& next_plane() { ++m_p; return *this; }
    inline ProceduralPixelAccessor& prev_plane() { --m_p; return *this; }
    inline ProceduralPixelAccessor& advance( offset_type dc, offset_type dr, int32 dp=0 ) { m_c+=dc; m_r+=dr; m_p+=dp; return *this; }

    inline result_type operator*() const { return m_view(m_c,m_r,m_p); }
  };

} // namespace vw

#endif // __VW_IMAGE_PIXELACCESSORS_H__
