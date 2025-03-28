#include <rtc/rtc.hpp>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <sys/select.h>

typedef int SOCKET;

using nlohmann::json;

const int BUFFER_SIZE = 2048; // Good size to receive ethernet packages

std::atomic<bool> running{true};

void receiveVideo (SOCKET sock, std::shared_ptr<rtc::Track> videoTrack, rtc::SSRC ssrcVideo) {
    char videoBuffer[BUFFER_SIZE];

    while(running) {
        int videoLen = recv(sock, videoBuffer, BUFFER_SIZE, 0);

        if (videoLen < sizeof(rtc::RtpHeader) || !(videoTrack->isOpen())) {
            continue;
        }

        auto video_rtp = reinterpret_cast<rtc::RtpHeader *>(videoBuffer);
        video_rtp->setSsrc(ssrcVideo);
        videoTrack->send(reinterpret_cast<const std::byte *>(videoBuffer), videoLen);
    }

}

void receiveAudio (SOCKET sock, std::shared_ptr<rtc::Track> audioTrack, rtc::SSRC ssrcAudio) {
    char audioBuffer[BUFFER_SIZE];

    while(running) {
        int audioLen = recv(sock, audioBuffer, BUFFER_SIZE, 0);

        if (audioLen < sizeof(rtc::RtpHeader) || !(audioTrack->isOpen())) {
            continue;
        }

        auto audio_rtp = reinterpret_cast<rtc::RtpHeader *>(audioBuffer);
        audio_rtp->setSsrc(ssrcAudio);
        audioTrack->send(reinterpret_cast<const std::byte *>(audioBuffer), audioLen);
    }
}



int main() {

    try {
        rtc::InitLogger(rtc::LogLevel::Debug); // Started the logging at debug level
        auto pc = std::make_shared<rtc::PeerConnection>(); // Pointer to my PeerConnection

        pc->onStateChange([](rtc::PeerConnection::State state) {
            std::cout << "State of the connection: " << state << std::endl;
        });

        pc->onGatheringStateChange([pc](rtc::PeerConnection::GatheringState state) {
            std::cout << "ICE State: " << state << std::endl;
        });

        pc->onStateChange([](rtc::PeerConnection::State state) {
            std::cout << "PeerConnection state changed to: " << state << std::endl;
        });

        const rtc::SSRC video_ssrc = 43;
        rtc::Description::Video video("video", rtc::Description::Direction::SendOnly);
        video.addH264Codec(96);
        video.addSSRC(video_ssrc, "video-send");
        auto video_track = pc->addTrack(video);

        const rtc::SSRC audio_ssrc = 44;
        rtc::Description::Audio audio("audio", rtc::Description::Direction::SendOnly);
        audio.addOpusCodec(97);
        audio.addSSRC(audio_ssrc, "audio-send");
        auto audio_track = pc->addTrack(audio);

        std::cout << "Video track SSRC: " << video_ssrc << std::endl;
        std::cout << "Audio track SSRC: " << audio_ssrc << std::endl;

        pc->setLocalDescription();
        auto localOffer = pc->localDescription();
        json offerJson = {{"type", localOffer->typeString()},
                            {"sdp", std::string(localOffer.value())}};
        std::string offerStr = offerJson.dump();

        std::string signalingIP;
        std::cout << "Insert the signaling server IP: ";
        std::cin >> signalingIP;

        SOCKET video_socket = socket(AF_INET, SOCK_DGRAM, 0);
        SOCKET audio_socket = socket(AF_INET, SOCK_DGRAM, 0);
        SOCKET sdp_socket = socket(AF_INET, SOCK_DGRAM, 0);
        if (sdp_socket < 0) {
            throw std::runtime_error("Failed to create client socket");
        }

        struct sockaddr_in videoAddr = {};
        videoAddr.sin_family = AF_INET;
        videoAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        videoAddr.sin_port = htons(6000);
        
        struct sockaddr_in audioAddr = {};
        audioAddr.sin_family = AF_INET;
        audioAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
        audioAddr.sin_port = htons(6001);

        int VideoRcvBufSize = 512*1024;
        int AudioRcvBufSize = 128*1024;
        int sdpRcvBufSize = 65536; // 64 KB (suficiente para SDP)
        setsockopt(video_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&VideoRcvBufSize), sizeof(VideoRcvBufSize));
        setsockopt(audio_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char *>(&AudioRcvBufSize), sizeof(AudioRcvBufSize));
        setsockopt(sdp_socket, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&sdpRcvBufSize), sizeof(sdpRcvBufSize));


        if (bind(video_socket, reinterpret_cast<const sockaddr *>(&videoAddr), sizeof(videoAddr)) < 0) {
            throw std::runtime_error("Failed to bind the UDP video socket, port 6000");
        }

        if (bind(audio_socket, reinterpret_cast<const sockaddr *>(&audioAddr), sizeof(audioAddr)) < 0) {
            throw std::runtime_error("Failed to bind the UDP audio socket, port 6001");
        }

        struct sockaddr_in sdpAddr = {};
        sdpAddr.sin_family = AF_INET;
        sdpAddr.sin_addr.s_addr = inet_addr(signalingIP.c_str());
        sdpAddr.sin_port = htons(5000);

        struct timeval tv;
        tv.tv_sec = 10; // Timeout de 10 segundos
        tv.tv_usec = 0;
        setsockopt(sdp_socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

        std::cout << "Conecting to server: " << signalingIP << "..." << std::endl;
        if (connect(sdp_socket, reinterpret_cast<const sockaddr *>(&sdpAddr), sizeof(sdpAddr)) < 0) {
            throw std::runtime_error("Failed to connect to server.");
        }
        std::cout << "Connection established." << std::endl;

        std::cout << "Sending SDP offer..." << std::endl;
        send(sdp_socket, offerStr.c_str(), offerStr.size(), 0);

        char buffer[BUFFER_SIZE];
        std::cout << "Waiting an SDP answer..." << std::endl;
        int bytesReceived = recv(sdp_socket, buffer, BUFFER_SIZE, 0);
        if (bytesReceived <= 0) {
            throw std::runtime_error("Failed to receive SDP answer");
        } else if (bytesReceived >= BUFFER_SIZE) {
            throw std::runtime_error("SDP answer too large");
        }

        std::string answerStr(buffer, bytesReceived);
        json answerJson = json::parse(answerStr);
        std::cout << "Received SDP answer:\n" << answerJson << std::endl;

        rtc::Description answer(answerJson["sdp"].get<std::string>(), answerJson["type"].get<std::string>());
        try {
            pc->setRemoteDescription(answer);
        } catch (const std::exception& e) {
            std::cerr << "Failed to set remote description: " << e.what() << std::endl;
            running = false;
        }

        std::cout << "WebRTC connection established with success!" << std::endl;

        std::thread videoThread(receiveVideo, video_socket, video_track, video_ssrc);
        std::thread audioThread(receiveAudio, audio_socket, audio_track, audio_ssrc);

        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        videoThread.join();
        audioThread.join();
        close(video_socket);
        close(audio_socket);
        close(sdp_socket);

    } catch (const std::exception& e) {
        running = false;
        return -1;
    }
    return 0;
}