#!/bin/sh

case "$1" in
    start)
        echo "Starting aesdsocket"
        # Start the aesdsocket service as dameon process using -d option using start-stop-daemon
        start-stop-daemon -S usr/bin/aesdsocket -- -d
        ;;
    stop)
        echo "Stopping aesdsocket"
        # Stop the aesdsocket service
        start-stop-daemon -K usr/bin/aesdsocket
        ;;
    restart)
        echo "Restarting aesdsocket"
        # Restart the aesdsocket service
        start-stop-daemon -K usr/bin/aesdsocket
        sleep 1
        start-stop-daemon -S usr/bin/aesdsocket -- -d
        ;;
    status)
        # Check the status of the aesdsocket service
        if pgrep usr/bin/aesdsocketd > /dev/null; then
            echo "usr/bin/aesdsocket is running"
        else
            echo "usr/bin/aesdsocket is not running"
        fi
        ;;
    *)
        echo "Usage: $0 {start|stop|restart|status}"
        exit 1
        ;;
esac