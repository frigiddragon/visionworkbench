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

/// \file Exception.h
/// 
/// Exception classes and related functions and macros.
///
/// The Vision Workbench is intended in part to be used in flight
/// systems, experimental multiprocessor systems, or other
/// environments where exceptions may not be fully supported.  As a
/// result, the use of exceptions within the Vision Workbench is
/// tightly controlled.  In particular, the exception usage rules were
/// designed to minimize the impact on platforms that do not support
/// exceptions at all.  There is a standard Vision Workbench
/// "exception" class hierarchy which is used to describe errors and
/// can be used even on platforms that do not support the C++
/// exception system.
/// 
/// The vw::Exception class serves as a base class for all VWB error
/// types.  It is designed to make it easy to throw exceptions with 
/// meaningful error messages.  For example, this invocation:
///
///  <TT>vw_throw( vw::Exception() << "Unable to open file \"" << filename << "\"!" );</TT>
///
/// might generate a message like this:
///
///  <TT>terminate called after throwing an instance of 'vw::Exception'</TT>
///  <TT>     what():  Unable to open file "somefile.foo"! </TT>
///
/// A variety of standard derived exception types are provided; in the
/// above example, the exception should probably have been of type
/// vw::IOErr.  Also, two macros, VW_ASSERT(condition,exception) and
/// VW_DEBUG_ASSERT(condition,exception), are provided, with the usual
/// assertion semantics.  The only difference is that the debug
/// assertions will be disabled for increased performance in release
/// builds when VW_DEBUG_LEVEL is defined to zero (which happens by
/// default when NDEBUG is defined).
///
/// Note that in the example the exception was thrown by calling the
/// vw_throw() function rather than by using the C++ throw statement.
/// On platforms that do support C++ exceptions the default behavior
/// for vw_throw() is to throw the exception in the usual way.
/// However, the user can provide their own error-handling mechanism
/// if they choose.  For example, the default behavior when exceptions
/// are disabled is to print the error text to stderr and call
/// abort().
///
/// In general the only allowed usage of exceptions within the Vision
/// Workbench is throwing them using vw_throw().  In particular, try
/// and catch blocks are generally prohibited, so exceptions can only
/// be used to report fatal errors that the library is unable to
/// recover from by itself.  Other uses of exceptions are allowed only
/// under a few special circumstances.  If a part of the Vision
/// Workbench depends on a third-party library that fundamentally
/// relies on exceptions, then that part of the Vision Workbench may
/// use exceptions as well.  However, in that case that entire portion
/// of the Vision Workbench must be disabled when exceptions are not
/// supported.  Similarly, if a part of the Vision Workbench that
/// provides a high-level service cannot reasonably be written without
/// the full use of exceptions, then this portion may also be disabled
/// on platforms without exceptions.  In both of these cases it must
/// be clearly documented that these features are not available on
/// platforms that do not support exceptions.  Finally, it is legal to
/// catch an exception within the library for the sole purpose of
/// re-throwing an exception with a more informative data type or
/// error message.  This purely cosmetic usage must be conditionally
/// compiled like this:
///  #if defined(VW_ENABLE_EXCEPTIONS) && (VW_ENABLE_EXCEPTIONS==1) )
/// Obviously this functionality will be disabled on platforms 
/// that do not support exceptions.
///
/// Exceptions are enabled or disabled based on the value of the
/// VW_ENABLE_EXCEPTIONS macro defined in vw/config.h.  This value can be
/// set by passing the command line options --enable-exeptions (the
/// default) or --disable-exceptions to the configure script prior to
/// buliding the Vision Workbench.  This option also sets an automake
/// variable called ENABLE_EXCEPTIONS which may be used by the build
/// system to conditionally compile entire source files.
///
/// In either case the default behavior of vw_throw() may be
/// overridden by calling set_exception_handler(), passing it a
/// pointer to a user-defined object derived from ExceptionHandler.
/// The user specifies the error-handling behavior by overriding the
/// abstract method handle().  When exceptions have not been disabled,
/// the Exception class and its children define a virtual method
/// default_throw() which the handler may call to have the exception
/// throw itself in a type-aware manner.
///
#ifndef __VW_CORE_EXCEPTION_H__
#define __VW_CORE_EXCEPTION_H__

#include <string>
#include <sstream>
#include <ostream>

#include <vw/config.h>

#if defined(VW_ENABLE_EXCEPTIONS) && (VW_ENABLE_EXCEPTIONS==1)
#include <exception>
#define VW_IF_EXCEPTIONS(x) x
#else
#define VW_IF_EXCEPTIONS(x)
#endif

namespace vw {

  /// The core exception class.  
  struct Exception VW_IF_EXCEPTIONS( : public std::exception )
  {

    /// The default constructor generates exceptions with empty error
    /// message text.  This is the cleanest approach if you intend to
    /// use streaming (via operator <<) to generate your message.
    Exception() VW_IF_EXCEPTIONS(throw()) {}

    /// Generates exceptions with the given error message text.
    Exception( std::string const& s ) VW_IF_EXCEPTIONS(throw()) { m_desc << s; }

    virtual ~Exception() VW_IF_EXCEPTIONS(throw()) {}

    /// Copy Constructor
    Exception( Exception const& e ) VW_IF_EXCEPTIONS(throw())
      VW_IF_EXCEPTIONS( : std::exception(e) ) {
      m_desc << e.m_desc.str();
    }

    /// Assignment operator copies the error string.
    Exception& operator=( Exception const& e ) VW_IF_EXCEPTIONS(throw()) {
      m_desc.str( e.m_desc.str() );
      return *this;
    }

    /// Returns a the error message text for display to the user.  The
    /// returned pointer must be used immediately; other operations on
    /// the exception may invalidate it.  If you need the data for
    /// later, you must save it to a local buffer of your own.
    virtual const char* what() const VW_IF_EXCEPTIONS(throw()) {
      m_what_buf = m_desc.str();
      return m_what_buf.c_str();
    }

    /// Returns the error message text as a std::string.
    std::string desc() const { return m_desc.str(); }

    VW_IF_EXCEPTIONS( virtual void default_throw() const { throw *this; } )

  protected:
    // The error message text.
    std::ostringstream m_desc;

    // A buffer for storing the full exception description returned by
    // the what() method, which must generate its return value from
    // the current value of m_desc.  The what() method provides no 
    // mechanism for freeing the returned string, and so we handle 
    // allocation of that memory here, internally to the exception.
    mutable std::string m_what_buf;
  };

  // Use this macro to construct new exception types that do not add
  // additional functionality.  If you can think of a clean way to do
  // this using templates instead of the preprocessor, please do.  For
  // now, we're stuck with this.
  //
  // Some functions need to return the *this pointer with the correct
  // subclass type, and these are defined in the macro below rather
  // than the base exception class above.   These are:
  //
  // Exception::operator=():
  // The assignment operator must return an instance of the subclass.
  //
  // Exception::operator<<():
  // The streaming operator (<<) makes it possible to quickly
  // generate error message text.  This is currently implemented 
  // by simply forwarding invocations of this method to an 
  // internal ostringstream.
  //
  // Exception::set():
  // Sets the error message text to the provided string, returning a
  // reference to the exception for use with the streaming operator
  // (<<) if desired.
  //
  // Exception::reset():
  // Resets (i.e. clears) the error message text, returning a
  // reference to the exception for use with the streaming operator
  // (<<) if desired

  /// Macro for quickly creating a hierarchy of exceptions, all of
  /// which share the same functionality.
  #define VW_DEFINE_EXCEPTION(name,base)                              \
  struct name : public base {                                         \
    name() VW_IF_EXCEPTIONS(throw()) : base() {}                      \
    name(std::string const& s) VW_IF_EXCEPTIONS(throw()) : base(s) {} \
    name( name const& e ) VW_IF_EXCEPTIONS(throw()) : base( e ) {}    \
    virtual ~name() VW_IF_EXCEPTIONS(throw()) {}                      \
                                                                      \
    inline name& operator=( name const& e ) VW_IF_EXCEPTIONS(throw()) { \
      base::operator=( e );                                           \
      return *this;                                                   \
    }                                                                 \
                                                                      \
    template <class T>                                                \
    name& operator<<( T const& t ) { m_desc << t; return *this; }     \
                                                                      \
    name& set( std::string const& s ) { m_desc.str(s);  return *this; } \
                                                                      \
    name& reset() { m_desc.str("");  return *this; }                  \
                                                                      \
    VW_IF_EXCEPTIONS(virtual void default_throw() const { throw *this; }) \
  }

  /// Invalid function argument exception
  VW_DEFINE_EXCEPTION(ArgumentErr, Exception);

  /// Incorrect program logic exception
  VW_DEFINE_EXCEPTION(LogicErr, Exception);

  /// Invalid program input exception
  VW_DEFINE_EXCEPTION(InputErr, Exception);

  /// IO failure exception
  VW_DEFINE_EXCEPTION(IOErr, Exception);

  /// Arithmetic failure exception
  VW_DEFINE_EXCEPTION(MathErr, Exception);

  /// Unexpected null pointer exception
  VW_DEFINE_EXCEPTION(NullPtrErr, Exception);

  /// Invalid type exception
  VW_DEFINE_EXCEPTION(TypeErr, Exception);

  /// Not found exception
  VW_DEFINE_EXCEPTION(NotFoundErr, Exception);

  /// Unimplemented functionality exception
  VW_DEFINE_EXCEPTION(NoImplErr, Exception);

  /// Operation aborted partway through (e.g. with ProgressCallback returning Abort)
  VW_DEFINE_EXCEPTION(Aborted, Exception);


  /// The abstract exception handler base class, which users  
  /// can subclass to install an alternative exception handler.
  class ExceptionHandler {
  public:
    virtual void handle( Exception const& e ) const = 0;
    virtual ~ExceptionHandler() {}
  };

  /// Sets the application-wide exception handler.  Pass zero 
  /// as an argument to reinstate the default handler.  The 
  /// default behavior is to throw the exception unless the 
  /// VW_ENABLE_EXCEPTIONS macro in vw/config.h was defined to 0
  /// at build time, in which case the default behavior is to 
  /// print the error message at the ErrorMessage level and 
  /// to call abort().
  void set_exception_handler( ExceptionHandler const* eh );

  /// Throws an exception via the Vision Workbench error 
  /// handling mechanism, which may not actually involvle 
  /// throwing an exception in the usual C++ sense.
  void vw_throw( Exception const& e );

} // namespace vw

/// The VW_ASSERT macro throws the given exception if the given
/// condition is not met.  The VW_DEBUG_ASSERT macro does the same
/// thing, but is disabled if VW_DEBUG_LEVEL is zero.  The default
/// value for VW_DEBUG_LEVEL is defined in Debugging.h.
#define VW_ASSERT(cond,excep) do { if(!(cond)) vw_throw( excep ); } while(0)
#if VW_DEBUG_LEVEL == 0
#define VW_DEBUG_ASSERT(cond,excep) do {} while(0)
#else
// Duplicate the definition to avoid extra macro expansion in recusion
#define VW_DEBUG_ASSERT(cond,excep) do { if(!(cond)) vw_throw( excep ); } while(0)
#endif

#endif // __VW_CORE_EXCEPTION_H__
