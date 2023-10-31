include <iostream>
#include <thread>
#include <string>

#include <unistd.h>
// Muuttuja, johon tallennetaan viesti
std::string message;
// Muuttuja, joka kertoo onko viesti käsitelty
bool messageReady = false;

// Lähettäjäsäie
void sender() {
    while (true) {
        std::string input;
        std::cout << "Lähettäjä: ";
        // Tallennetaan lähettäjän syöte
        std::getline(std::cin, input);
        message = input;
        // Viesti lähetetty
        messageReady = true;

        if (input == "exit") {
            break;
        }
    }
}

// Vastaanottajasäie
void receiver() {
    while (true) {
        if (messageReady) {  
            // Tarkistetaan, onko viesti valmis
            std::string receivedMessage = message;  
            // Luetaan viesti ja tallennetaan se
            messageReady = false;
            // Viesti on käsitelty

            if (receivedMessage == "exit") {
                break;
            }
            // Tulostetaan vastaanotettu viesti
            std::cout << "Vastaanottaja: " << receivedMessage << std::endl;

            // Tarkista, onko viesti oikein
            if (receivedMessage == "Terve") {
                // Lähetä ACK
                std::cout << "ACK lähetetty" << std::endl;
            } else {
                // Lähetä NACK
                std::cout << "NACK lähetetty" << std::endl;
            }
        }
    }
}

int main() {
    // Lähettäjäsäie
    std::thread senderThread(sender);
    // Vastaanottajasäie
    std::thread receiverThread(receiver);
    // Odota, että lähettäjäsäie päättyy
    senderThread.join();
    // Odota, että vastaanottajasäie päättyy
    receiverThread.join();
 //sleep(5);
    return 0;
}
