#include <libssh2_ruby.h>

/*
 * Helpers to return the LIBSSH2_CHANNEL for the given instance.
 * */
static inline LIBSSH2_CHANNEL *
get_channel(VALUE self) {
    LibSSH2_Ruby_Channel *channel_data;
    Data_Get_Struct(self, LibSSH2_Ruby_Channel, channel_data);
    return channel_data->channel;
}

/*
 * Deallocates the memory associated with the channel data
 * structure. This also properly lowers the reference count
 * on the session structure.
 * */
static void
channel_dealloc(LibSSH2_Ruby_Channel *channel_data) {
    if (channel_data->channel != NULL) {
        BLOCK(libssh2_channel_free(channel_data->channel));
    }

    if (channel_data->session != NULL) {
        libssh2_ruby_session_release(channel_data->session);
    }

    free(channel_data);
}

/*
 * Called to allocate the memory associated with channels. We allocate
 * some space to store pointers to libssh2 structs in here.
 * */
static VALUE
allocate(VALUE self) {
    LibSSH2_Ruby_Channel *channel = malloc(sizeof(LibSSH2_Ruby_Channel));
    channel->channel = NULL;
    channel->session = NULL;

    return Data_Wrap_Struct(self, 0, channel_dealloc, channel);
}

/*
 * call-seq:
 *     Channel.new(session)
 *
 * Creates a new channel for the given session. This will open
 * a channel on the session so the session must be ready for that
 * or an exception will be raised.
 *
 * */
static VALUE
initialize(VALUE self, VALUE rb_session) {
    LIBSSH2_SESSION *session;
    LibSSH2_Ruby_Channel *channel_data;

    // Verify we have a valid session object
    CHECK_SESSION(rb_session);

    // Get the internal data from the instance.
    Data_Get_Struct(self, LibSSH2_Ruby_Channel, channel_data);

    // Read the interal data from the session
    Data_Get_Struct(rb_session, LibSSH2_Ruby_Session, channel_data->session);
    session = channel_data->session->session;

    // Create the channel, which we always do in a blocking
    // fashion since there is no other opportunity.
    do {
        channel_data->channel = libssh2_channel_open_session(session);

        // If we don't have a channel ant don't have a EAGAIN
        // error, then we raise an exception.
        if (channel_data->channel == NULL) {
            int error = libssh2_session_last_error(session, NULL, NULL, 0);
            if (error != LIBSSH2_ERROR_EAGAIN) {
                rb_exc_raise(libssh2_ruby_wrap_error(error));
                return Qnil;
            }
        }
    } while(channel_data->channel == NULL);

    // Increase the refcount of the session data for us now that
    // we have a channel.
    libssh2_ruby_session_retain(channel_data->session);

    return self;
}

/*
 * call-seq:
 *     channel.exec("echo foo") -> Qtrue
 *
 * Executes a command line method on the channel.
 *
 * */
static VALUE
exec(VALUE self, VALUE command) {
    int result;

    rb_check_type(command, T_STRING);

    result = libssh2_channel_exec(get_channel(self), StringValuePtr(command));
    HANDLE_LIBSSH2_RESULT(result);
}

/*
 * call-seq:
 *     channel.read -> [string, result_code]
 *
 * Reads from the channel. This will return the data it got as a string,
 * along with the result code.
 *
 * */
static VALUE
read(VALUE self) {
    int result;
    char buffer[0x4000];
    VALUE data = rb_str_buf_new(0x2000);
    LIBSSH2_CHANNEL *channel = get_channel(self);

    do {
        // Read from the channel
        result = libssh2_channel_read(channel, buffer, sizeof(buffer));

        if (result > 0) {
            // Read succeeded, append to our string with the same number
            // of bytes that libssh2 said that we read.
            data = rb_str_buf_cat(data, buffer, result);
        } else if (result < 0) {
            // If we got EAGAIN, then its not a problem, we just re-loop.
            // Otherwise, we throw an error!
            if (result != LIBSSH2_ERROR_EAGAIN) {
                rb_exc_raise(libssh2_ruby_wrap_error(result));
                return Qnil;
            }
        }
    } while(result > 0);

    return rb_ary_new3(2, data, INT2FIX(result));
}

void init_libssh2_channel() {
    VALUE cChannel = rb_cLibSSH2_Native_Channel;
    rb_define_alloc_func(cChannel, allocate);
    rb_define_method(cChannel, "initialize", initialize, 1);
    rb_define_method(cChannel, "exec", exec, 1);
    rb_define_method(cChannel, "read", read, 0);
}
