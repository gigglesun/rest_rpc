#pragma once

namespace timax { namespace rpc 
{
	connection::connection(io_service_t& ios, duration_t time_out)
		: ios_wrapper_(ios)
		, socket_(ios)
		, read_buffer_(PAGE_SIZE)
		, timer_(ios)
		, time_out_(time_out)
	{
	}

	void connection::response(context_ptr& ctx)
	{
		auto self = this->shared_from_this();
		ios_wrapper_.write(self, ctx);
	}

	void connection::close()
	{
		boost::system::error_code ignored_ec;
		socket_.close(ignored_ec);
	}

	tcp::socket& connection::socket()
	{
		return socket_;
	}

	void connection::start()
	{
		set_no_delay();
		read_head();
	}

	void connection::on_error(boost::system::error_code const& error)
	{
		//SPD_LOG_DEBUG(error.message().c_str());

		close();

		decltype(auto) on_error = get_on_error();

		if (on_error)
			on_error(this->shared_from_this(), error);
	}

	void connection::set_on_error(connection_on_error_t on_error)
	{
		get_on_error() = std::move(on_error);
	}

	void connection::set_on_read(connection_on_read_t on_read)
	{
		get_on_read() = std::move(on_read);
	}

	connection::connection_on_error_t& connection::get_on_error()
	{
		static connection_on_error_t on_error;
		return on_error;
	}

	connection::connection_on_read_t& connection::get_on_read()
	{
		static connection_on_read_t on_read;
		return on_read;
	}

	blob_t connection::get_read_buffer() const
	{
		return{ read_buffer_.data(), head_.len };
	}

	req_header const& connection::get_read_header() const
	{
		return head_;
	}

	void connection::set_no_delay()
	{
		boost::asio::ip::tcp::no_delay option(true);
		boost::system::error_code ec;
		socket_.set_option(option, ec);
	}

	void connection::expires_timer()
	{
		if (time_out_.count() == 0)
			return;

		timer_.expires_from_now(time_out_);
		// timer_.async_wait
	}

	void connection::cancel_timer()
	{
		if (time_out_.count() == 0)
			return;

		timer_.cancel();
	}

	void connection::read_head()
	{
		expires_timer();
		async_read(socket_, boost::asio::buffer(&head_, sizeof(head_)),
			boost::bind(&connection::handle_read_head, this->shared_from_this(), asio_error));
	}

	void connection::read_body()
	{
		if (head_.len > MAX_BUF_LEN)
		{
			socket_.close();
			return;
		}

		if (head_.len > PAGE_SIZE)
		{
			read_buffer_.resize(head_.len);
		}

		async_read(socket_, boost::asio::buffer(read_buffer_.data(), head_.len),
			boost::bind(&connection::handle_read_body, this->shared_from_this(), asio_error));
	}

	void connection::handle_read_head(boost::system::error_code const& error)
	{
		if (!socket_.is_open())
			return;

		if (!error)
		{
			if (head_.len == 0)
			{
				read_head();
			}
			else
			{
				read_body();
			}
		}
		else
		{
			cancel_timer();
			on_error(error);
		}
	}

	void connection::handle_read_body(boost::system::error_code const& error)
	{
		cancel_timer();
		if (!socket_.is_open())
			return;

		if (!error)
		{
			decltype(auto) on_read = get_on_read();
			if (on_read)
				on_read(this->shared_from_this());

			read_head();
		}
		else
		{
			cancel_timer();
			on_error(error);
		}
	}
} }