//
// Copyright (c) 2008-2009 Marat Abrarov (abrarov@mail.ru)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef MA_ECHO_DETAIL_SESSION_HPP
#define MA_ECHO_DETAIL_SESSION_HPP

#include <boost/utility.hpp>
#include <boost/thread.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/bind.hpp>
#include <boost/ref.hpp>
#include <boost/asio.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/detail/atomic_count.hpp>
#include <ma/handler_allocation.hpp>
#include <ma/handler_invoke_helpers.hpp>
#include <ma/work_handler.hpp>
#include <ma/handler_storage.hpp>

namespace ma
{    
  namespace echo
  {
    namespace detail
    { 
      template <typename AsyncStream>
      class session : private boost::noncopyable 
      {
      private:
        typedef session<AsyncStream> this_type;
        typedef handler_storage<boost::system::error_code> handler_storage_type;

      public:
        typedef AsyncStream next_layer_type;
        typedef typename next_layer_type::lowest_layer_type lowest_layer_type;
        typedef boost::intrusive_ptr<this_type> pointer;     

        explicit session(boost::asio::io_service& io_service)
          : ref_count_(0)
          , io_service_(io_service)
          , strand_(io_service)
          , stream_(io_service)
          , mutex_()
          , closed_(false)
          , shutdown_handler_(0)
          , wait_handler_(0)
          , handshake_done_(0)
          , shutdown_done_(0)
          , writing_(false)
          , reading_(false)
          , write_handler_allocator_()
          , read_handler_allocator_()
          , last_read_error_() 
          , last_write_error_() 
        {
          //todo
        }

        ~session()
        {          
        }

        next_layer_type& next_layer()
        {
          return stream_;
        }

        lowest_layer_type& lowest_layer()
        {
          return stream_.lowest_layer();
        }

        // Terminate all user-defined pending operations.
        void close(boost::system::error_code& error)
        {
          error = boost::system::error_code();
          boost::mutex::scoped_lock lock(mutex_);
          closed_ = true;

          // Close all user operations.            
          if (wait_handler_)
          {
            wait_handler_(boost::asio::error::operation_aborted);
          }            
          if (shutdown_handler_)
          {
            shutdown_handler_(boost::asio::error::operation_aborted);
          }

          // Close internal operations.        
          stream_.close(error);          
        }

        this_type* prev_;
        this_type* next_; 

        template <typename Handler>
        void async_handshake(Handler handler)
        {        
          strand_.dispatch(make_context_alloc_handler(handler,
            boost::bind(&this_type::handshake<Handler>, pointer(this), boost::make_tuple(handler))));
        }

        template <typename Handler>
        void async_shutdown(Handler handler)
        {
          strand_.dispatch(make_context_alloc_handler(handler,
            boost::bind(&this_type::shutdown<Handler>, pointer(this), boost::make_tuple(handler))));
        }        

        template <typename Handler>
        void async_wait(Handler handler)
        {        
          strand_.dispatch(make_context_alloc_handler(handler,
            boost::bind(&this_type::wait<Handler>, pointer(this), boost::make_tuple(handler))));
        }

      private:
        friend void intrusive_ptr_add_ref(this_type* ptr)
        {
          ++ptr->ref_count_;
        }

        friend void intrusive_ptr_release(this_type* ptr)
        {
          if (0 == --ptr->ref_count_)
          {
            delete ptr;
          }
        }

        template <typename Handler>
        void handshake(boost::tuple<Handler> handler)
        {        
          boost::mutex::scoped_lock lock(mutex_);
          if (closed_)
          {          
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::operation_aborted));
          }          
          else if (handshake_done_)
          {
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::already_connected));
          }
          else
          {          
            if (!reading_)
            {
              start_read();          
            }
            handshake_done_ = true;
            // Make the upcall
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::system::error_code()));              
          }
        }

        template <typename Handler>
        void shutdown(boost::tuple<Handler> handler)
        {
          boost::mutex::scoped_lock lock(mutex_);
          if (closed_)
          {
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::operation_aborted));
          }
          else if (shutdown_handler_)
          {
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::already_started));
          }
          else if (!handshake_done_)
          {
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::not_connected));
          }
          else if (shutdown_done_)
          {
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::shut_down));
          }
          else if (wait_handler_)
          {
            //todo
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::operation_not_supported));
          }
          else 
          {
            //todo
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::operation_not_supported));
          }
        }

        template <typename Handler>
        void wait(boost::tuple<Handler> handler)
        {
          boost::mutex::scoped_lock lock(mutex_);
          if (closed_)
          {
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::operation_aborted));
          }
          else if (wait_handler_)
          {
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::already_started));
          }
          else if (!handshake_done_)
          {
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::not_connected));
          }
          else if (shutdown_done_)
          {
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::shut_down));
          }
          else if (shutdown_handler_)
          {
            //todo
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::operation_not_supported));
          }
          else 
          {
            //todo
            io_service_.post(boost::asio::detail::bind_handler(
              boost::get<0>(handler), boost::asio::error::operation_not_supported));
          }
        }

        void start_read()
        {
          //todo
        }

        boost::detail::atomic_count ref_count_;      
        boost::asio::io_service& io_service_;
        boost::asio::io_service::strand strand_;
        next_layer_type stream_;       
        boost::mutex mutex_;
        bool closed_;
        handler_storage_type shutdown_handler_;
        handler_storage_type wait_handler_;
        bool handshake_done_;
        bool shutdown_done_;
        bool writing_;
        bool reading_;
        handler_allocator write_handler_allocator_;
        handler_allocator read_handler_allocator_;
        boost::system::error_code last_read_error_;      
        boost::system::error_code last_write_error_;      

      }; // class session

    } // namespace detail
  } // namespace echo
} // namespace ma

#endif // MA_ECHO_DETAIL_SESSION_HPP
