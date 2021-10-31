// -*- coding: utf-8 -*-
//
// EspHome Midea Air Conditioner cpp
//
// Copyright 2020-2020 uncle-yura uncle-yura@tuta.io
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//


#include "midea.h"
using namespace esphome;

// Public Methods //////////////////////////////////////////////////////////////

void Midea::on_state(int mode, int temp, int power, int speed, int hswing, int vswing) {
    #ifdef DEBUG
        ESP_LOGD("Debug", "State changed: %d %d %d %d %d %d",mode, temp, power, speed, hswing, vswing);
    #endif
    set_status(mode, temp, power, speed, hswing, vswing);
}

void Midea::setup() {
    register_service(&Midea::on_state, "state",{"mode", "temp", "power", "speed", "hswing", "vswing"});
    this->set_update_interval(1000);
}

void Midea::update() {
    if(queue_pointer_first<0)get_status();
    
    serial_buffer *current;
    current=fifo_get();
    
    switch(current->status) {
        case 0: //first attempt
        case 4: //resend after 3 seconds
        case 7: //resend after 6 seconds
            write_array(current->buffer,current->size);
            current->status++;
            log_data(current);
            break;
        case 10: //pop message
            fifo_pop();
            break;
        default:
            current->status++;
            break;
    }
}

void Midea::loop() {
    if (available()) {
    char c = read();
    
    switch(c) {
        case 0xAA:
            receive_data.size = 0;
            receive_data.buffer[receive_data.size] = c;
            receive_data.size ++;
            break;
        
        default:
            if(receive_data.size>0 && receive_data.buffer[receive_data.size-1]==0xAA) {
                receive_data.buffer[receive_data.size] = c;
                read_array(receive_data.buffer+2, c-1);
                receive_data.size = c+1;
                
                log_data(&receive_data);
                
                parse_answer();
            }
            break;
        }
    }
}


// Private Methods //////////////////////////////////////////////////////////////


void Midea::fifo_put(serial_buffer *packet) {
    if(queue_pointer_first>=0) {
        queue_pointer_last++;
        queue_pointer_last &= 0x0F;
        memcpy(&transmite_queue[queue_pointer_last],packet,sizeof(serial_buffer));
    }
    else {
        memcpy(&transmite_queue[0],packet,sizeof(serial_buffer));
        queue_pointer_first = 0;
      queue_pointer_last = 0;
    }
}

serial_buffer *Midea::fifo_get() {
    if(queue_pointer_first>0)return &transmite_queue[queue_pointer_first];
    else return &transmite_queue[0];
}

void Midea::fifo_pop() {
    memset(&transmite_queue[queue_pointer_first],0,sizeof(serial_buffer));
    if(queue_pointer_first==queue_pointer_last) {
        queue_pointer_first = -1;
        queue_pointer_last = -1;
    }
    else {
        queue_pointer_first++;
        queue_pointer_first &= 0x0F;
    }
}

byte Midea::calculate_crc (byte *data, int length) {
    byte crc = 0;
    for (int i = 0; i < length; i++) {
        crc = crc8Table[crc ^ data[i]];
    }
    return crc;
}

byte Midea::calculate_checksum (byte *data, int length) {
    byte sum = 0;
    for (int i = 0; i < length; i++) {
        sum += data[i];
    }
    return 256 - (sum % 256);
}

void Midea::array_to_string(byte array[], unsigned int len, char buffer[]) {
    for (unsigned int i = 0; i < len; i++) {
        byte nib1 = (array[i] >> 4) & 0x0F;
        byte nib2 = (array[i] >> 0) & 0x0F;
        buffer[i*2+0] = nib1  < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
        buffer[i*2+1] = nib2  < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
    }
    buffer[len*2] = '\0';
}

void Midea::clear_buffer(serial_buffer *buf) {
    memset(buf->buffer, 0, sizeof(buf->buffer));
    buf->size=0;
}

bool Midea::ckeck_crc(serial_buffer *buf) {
    if(calculate_crc(buf->buffer + 10, buf->size-11)==0x00)
        return true;
    else
        return false;
}

void Midea::set_crc(serial_buffer *buf) {
    buf->buffer[buf->size] = calculate_crc(buf->buffer + 10, buf->size-10);
    buf->size++;
}

bool Midea::ckeck_checksum(serial_buffer *buf) {
    if(calculate_checksum(buf->buffer + 1, buf->size-1)==0x00)
        return true;
    else
        return false;
}

void Midea::set_checksum(serial_buffer *buf) {
    buf->buffer[buf->size] = calculate_checksum(buf->buffer + 1, buf->size);
    buf->size++;
}

void Midea::log_data(serial_buffer *buf)
{
    #ifdef DEBUG
        char str[512] = "";
        array_to_string(buf->buffer, buf->size, str);
        ESP_LOGD("Debug", "Packet(%c, time:%ds): %s",buf->type, buf->status, str);
    #endif
}

void Midea::parse_answer() {
    if(ckeck_checksum(&receive_data) && ckeck_crc(&receive_data)) {
        if(fifo_get()->buffer[32] == receive_data.buffer[32]) fifo_pop();
        
        if(receive_data.buffer[10]==0xC0) {
            status.state = (receive_data.buffer[11] & 0x01) > 0;
            status.setpoint = (receive_data.buffer[12] & 0x0F) + 16 + ((receive_data.buffer[12] & 0x10) >> 4) * 0.5;
            status.mode = (receive_data.buffer[12] & 0xE0) >> 5;
            status.temp = (receive_data.buffer[21] - 50) / 2;
            
            if ((receive_data.buffer[13] & 0x7F) < 21) status.speed = 20;
            else if ((receive_data.buffer[13] & 0x7F) < 41) status.speed = 40;
            else if ((receive_data.buffer[13] & 0x7F) < 61) status.speed = 60;
            else if ((receive_data.buffer[13] & 0x7F) < 101) status.speed = 80;
            else status.speed = receive_data.buffer[13] & 0x7F;
            
            status.vswing = (receive_data.buffer[17] & 0x03) > 0;
            status.hswing = (receive_data.buffer[17] & 0x0C) > 0;
            
            if(pswitch->state != status.state) {
                pswitch->publish_state(status.state);
            }
            
            if(tsensor->state != status.temp) {
                tsensor->publish_state(status.temp);
            }
        }
    }
    clear_buffer(&receive_data);
}

void Midea::set_status(int mode, int temp, int power, int speed, int hswing, int vswing)
{
    serial_buffer transmite_data = {{0},0,'Q',0};
    
    clear_buffer(&transmite_data);
    
    memcpy(transmite_data.buffer, header_part, sizeof(header_part));
    transmite_data.size = sizeof(header_part);
    
    memcpy(transmite_data.buffer + sizeof(header_part), set_status_part, sizeof(set_status_part));
    transmite_data.size += sizeof(set_status_part);

    // Set packet length
    transmite_data.buffer[1] = transmite_data.size+1;
    // Change header mode to SET
    transmite_data.buffer[9] = 0x02;
    
    transmite_data.buffer[11] = 0x42 | (power<0 ? status.state : power);
    transmite_data.buffer[12] = (mode<0 ? status.mode << 5 : mode <<5) | (temp<0 ? status.setpoint-16 : temp - 16);
    transmite_data.buffer[13] = (speed<0 ? status.speed : speed);
    transmite_data.buffer[17] = 0x30 | (hswing<0 ? status.hswing ? 0x03 : 0x00 : hswing*0x03) | (vswing<0 ? status.vswing ? 0x0C : 0x00 : vswing*0x0C);
    // Set control byte
    transmite_data.buffer[32] = millis()/1000;

    set_crc(&transmite_data);
    set_checksum(&transmite_data);
    
    transmite_data.status = 0;
    
    fifo_put(&transmite_data);
}

void Midea::get_status()
{
    serial_buffer transmite_data = {{0},0,'Q',0};
    clear_buffer(&transmite_data);

    // Copy header part    
    memcpy(transmite_data.buffer, header_part, sizeof(header_part));
    transmite_data.size = sizeof(header_part);
    
    // Copy get status part
    memcpy(transmite_data.buffer + sizeof(header_part), get_status_part, sizeof(get_status_part));
    transmite_data.size += sizeof(get_status_part);
    
    // Set packet length
    transmite_data.buffer[1] = transmite_data.size+1; 
    
    // Change header mode to GET
    transmite_data.buffer[9] = 0x03;
    
    // Set control byte
    transmite_data.buffer[30] = millis()/1000;
    
    set_crc(&transmite_data);
    set_checksum(&transmite_data);
    
    transmite_data.status = 0;
    
    fifo_put(&transmite_data);
}
