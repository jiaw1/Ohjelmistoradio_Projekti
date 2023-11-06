/*
  simple tx rx test 
 - pregenerates samples
 - repeats same transmission
 - records samples 
 */

/** C++

compilation:

g++ -std=c++17 mainUE.cpp -lfftw3f -lfftw3 -luhd -lboost_system -lboost_program_options -pthread -orxTextMessage

*/


/**************************************************************
	Headers
**************************************************************/
#include "Headers.hpp"
// #include "mainHeaders.hpp"

#include "./PHY_UE.hpp"
// #include "../PHY/PHY_UE.hpp"

// Data processing
#include "./Data_processing.hpp"


using namespace std;

static std::atomic<bool> STOP_SIGNAL_CALLED;
/**************************************************************
**************************************************************/



/**************************************************************
	Functions
**************************************************************/
void os_signal_handler(int s){
  printf("\n Caught signal %d\n",s);

  STOP_SIGNAL_CALLED.store(true);
}



/**************************************************************
	Main
**************************************************************/
int UHD_SAFE_MAIN(int argc, char *argv[]) 
{
  int retVal = EXIT_SUCCESS;

  std::signal(SIGINT, os_signal_handler);

  InterThreadQueueSDR7320<TXitem> txQueue; 
  InterThreadQueueSDR7320<RXitem> rxQueue; 

  PHY_UE phyUE;
  phyUE.makeUEthread(txQueue,rxQueue);

  std::cout<<"UE main loops"<<std::endl;


  // random data generator
  int scaleSpectrum = RATE_SCALING;
  int NRBDL = 6*scaleSpectrum;
  int fft_size = 128*scaleSpectrum;
  RANDOM_DATA_FOR_PERFORMANCE_EST* testData = new RANDOM_DATA_FOR_PERFORMANCE_EST(fft_size,NRBDL);
  int nr_symbols = 6;
  int phy_packet_size = NRBDL*12/2 * nr_symbols;
 

  //*********************************************************************
  // Additional code
  //*********************************************************************

  bool No_error = false;
  bool data_received = false;

  //*********************************************************************
  //*********************************************************************

  int waitForTx=0;
  // while stop condition becomes true
  while(true){
    if(STOP_SIGNAL_CALLED.load()) break;

    std::this_thread::sleep_for(std::chrono::nanoseconds(9000));

    //***********************************************************************************
    // tx 
    //***********************************************************************************
    waitForTx++;
    if(!(waitForTx%10))
    {
      /* 
        Example data packet 
        Packet size - phy_packet_len bits  (int 0 or 1)            those are BPSK encoded in PHY   
        
        The example bits are binary bits of numbers from 0 - 99.  
      */
      {
      //******************************************************************
      // Data packet:
      //******************************************************************


      // 2 byte header, 104 byte payload and 2 byte crc. 1 packet = 108 bytes = 864 bits.
      //
      // Header symbols:
      //
      // symbol:    hex:     bin:          comment:
      //
      // ACK        0x06     00000110
      // NAC        0x15     00010101
      //
      // NUL        0x00     00000000      Data packet


      std::vector<int> txData(phy_packet_size);

      if (data_received) {

        if (No_error) {

          std::vector<int> header = to_int((Byte)0x06);
          for (int i = 0; i < phy_packet_size; i++) {
            
            if (i < 8) {
              txData[i] = header[i];
            }

            else if ( i < 16) {
              txData[i] = header[i - 8];
            }
            
            else {
              txData[i] = 0;
            }

          }

        }

        else {

          std::vector<int> header = to_int((Byte)0x15);
          for (int i = 0; i < phy_packet_size; i++) {
            
            if (i < 8) {
              txData[i] = header[i];
            }

            else if ( i < 16) {
              txData[i] = header[i - 8];
            }
            
            else {
              txData[i] = 0;
            }

          }

        }


      }

      No_error = false;
      data_received = false;

      /*
      std::cout << "Sent response: " << std::endl;
      for (auto it = txData.begin(); it != (txData.begin() + 8); it++) {
        std::cout << *it << " ";
      }
      std::cout << std::endl;
      for (auto it = (txData.begin() + 8); it != (txData.begin() + 16); it++) {
        std::cout << *it << " ";
      }
      std::cout << std::endl << std::endl;
      */

      auto txItem = std::unique_ptr<TXitem>(new TXitem());       // creates a send item that will hold the tx data
      txItem->insertData(std::move(txData));                     // put data into send item
      txItem->tx_meta_data.num_tx_symbols = 6;
      txQueue.writeItem(std::move(txItem));                      // send the bits vector
      }

    }
    /**************************************************************
    **************************************************************/



    //***********************************************************************************      
    // rx
    //***********************************************************************************
    {
    auto tmp = std::move(rxQueue.readItem()); // reads the received data from PHY
		if(tmp != nullptr)                        // received a data packet
		{
        
        
      std::vector<int> rx_bin_data;
      for (int i = 0; i < phy_packet_size; i++) {
        rx_bin_data.push_back((int)(tmp.get()->elem[i]<0));
      }


      /*
      for (auto it = rx_bin_data.begin(); it != rx_bin_data.end(); it++) {
        std::cout << *it << " ";
      }
      std::cout << std::endl << std::endl;
      */

      /*
      if(10*std::log10(std::abs((tmp.get()->rx_meta_data.pilot_power/tmp.get()->rx_meta_data.data_power))) < 3 )
      {
        testData->processRxData(&(tmp.get()->elem[0]),&(tmp.get()->rx_meta_data.elem_soft_bits[0]),tmp.get()->elem.size());
      }
      */


      tmp.reset();
      

      //****************************************************************************************
      // Data processing
      //****************************************************************************************

      data_received = true;

      std::pair<std::vector<int>, std::vector<int>> header;
      for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 8; j ++) {
          if(i == 0)
            header.first.push_back(rx_bin_data[(i * 8) + j]);
          else
            header.second.push_back(rx_bin_data[(i * 8) + j]);
        }
      }

      if((to_byte(header.first) == 0x00) && (to_byte(header.second) == 0x00)) {
        
        std::vector<int> pay_nd_crc(rx_bin_data);

        pay_nd_crc.erase(pay_nd_crc.begin(), pay_nd_crc.begin() + (2 * 8));

        std::vector<Byte> pay_nd_crc_B = to_byte_v(pay_nd_crc);

        //CRC check:
        No_error = checkCRC16(&pay_nd_crc_B[0], (104 + 2)) == 0;
        

        // Print bin data:
        /*
        std::cout << "Packet received! Bin data: " << std::endl;
        for (auto it = rx_bin_data.begin(); it != rx_bin_data.end(); it++) {
          std::cout << *it << " ";
        }
        std::cout << std::endl << std::endl;
        */

        // Print received message:
        if (No_error) std::cout << "Packet contained correct message: " << std::endl;
        else std::cout << "Packet contained CORRUPTED message: (nonprintable = '*' and null = ' ') " << std::endl;
        for (auto it = pay_nd_crc_B.begin(); it != (pay_nd_crc_B.end() - 2); it++) {
          if (std::isprint((char)*it)) {
            std::cout << (char)*it;
          }

          else {
            if ((char)*it == '\0') {
              std::cout << " ";
            } else {
              std::cout << "*";
            }
          }

        }
        std::cout << std::endl << std::endl;
      }
      //****************************************************************************************
      //****************************************************************************************

    }
    }
    /**************************************************************
    **************************************************************/
    

 }


  // send stop to the phy thread
  phyUE.stopProcessing();
 
  std::this_thread::sleep_for(std::chrono::nanoseconds(100) ); // placeholder for processing

  std::cout<<"Exit the main function"<<std::endl;

  return retVal;
}
