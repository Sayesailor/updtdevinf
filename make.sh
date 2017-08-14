g++ -std=c++11 updtdevinf.cc -I$MYSQLCPPCONN_INC -I$BOOST_INC -I$CUDA_INC  -L$MYSQLCPPCONN_LIB -L$CUDA_LIB/stubs -lmysqlcppconn -lnvidia-ml -o ./release/updtdevinf

