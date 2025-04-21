valgrind --error-exitcode=1 --leak-check=full --show-leak-kinds=all --track-origins=yes --errors-for-leak-kinds=definite --verbose --log-file=valgrind-out.txt ./aesdsocket
    rc=$?
    echo "Valgrind and aesdsocket complete"
    if [ $rc -ne 0 ]; then
        add_validate_error "Valgrind failed with $rc"
        echo "Valgrind output error log:"
        cat valgrind-out.txt
    fi