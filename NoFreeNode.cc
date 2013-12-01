//
// Copyright (c) 2013 Daniel Iñigo, Efren Suarez, Yuriy Batrakov, José Sklatz
//
// Permission is hereby granted, free of charge, to any
// person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice
// shall be included in all copies or substantial portions of
// the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY
// KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
// WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
// OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//

//
// file: NoFreeNode.cc
// author: Daniel Iñigo
//

#include <stdio.h>
#include <string.h>
#include <map>
#include <set>
#include <omnetpp.h>

#include "NoFreeNode.h"
#include "NoFreeMessage_m.h"

// Declara el módulo para que pueda usarse en el archivo de topología
Define_Module(NoFreeNode);

using namespace std;

NoFreeNode::NoFreeNode()
{
    // No necesita nada de momento.
}

NoFreeNode::~NoFreeNode()
{
    cancelAndDelete(reputationRequestTimer);
    cancelAndDelete(fileRequestTimer);
    cancelAndDelete(downloadFileTimer);
}

void NoFreeNode::initialize()
{
    requiredShareRate       = par("requiredShareRate");
    // Indicadores de archivo pedido a este nodo y siendo servido por él.
    nodeRequested = -1;
    nodeServed    = -1;
    // Lee los valores de las variables desde el archivo de topología.
    reputationTimeout       = par("reputationTiemout");
    reputationRequestTimeout= par("reputationRequestTimeout");
    fileRequestTimeout      = par("fileRequestTimeout");
    downloadFileTiemout     = par("downloadFileTiemout");
    kindness                = par("kindness");
    // Instancia los timer con un mensaje descriptivo.
    reputationRequestTimer  = new cMessage("reputationRequestTiemout");
    fileRequestTimer        = new cMessage("fileRequestTiemout");
    downloadFileTimer       = new cMessage("downloadFileTiemout");
    // Encolo la primera descarga dentro de un tiempo "downloadFileTiemout".
    downloadFileTiemout     = par("downloadFileTiemout");
    scheduleAt(simTime()+downloadFileTiemout, downloadFileTimer);
}

void NoFreeNode::fileRequest()
{
    // Elijo a la persona de entre los conectados a mi.
    int n = gateSize("dataGate$o");
    int k = intuniform(0,n-1);
    // Construyo un paquete.
    FileRequest *frmsg = new FileRequest("fileRequest");
    // Si la siguiente linea no está bien descomentar esta: gate("dataGate$o", n)->getChannel()->getSourceGate()->getOwnerModule()->getIndex();
    nodeRequested = gate("dataGate$o", n)->getNextGate()->getOwnerModule()->getIndex();
    frmsg->setSourceNodeId(getIndex());
    frmsg->setDestinationNodeId(n);
    // Le envío una petición.
    send(frmsg, "dataGate$o", k);
    // Si no tengo reputación del nodo al que pido, la creo.
    if(nodeMap.find(nodeRequested) == nodeMap.end()){
        nodeMap[nodeRequested].totalRequest    = 0;
        nodeMap[nodeRequested].acceptedRequest = 0;
    }
    // Considero que es un buen par y le subo la reputación para que no le
    // perjudique si alguien me la pide mientras tanto.
    nodeMap[nodeRequested].totalRequest++;
    nodeMap[nodeRequested].acceptedRequest++;
    // Encolo un nuevo evento dentro de un tiempo aleatorio.
    downloadFileTiemout = par("downloadFileTiemout");
    scheduleAt(simTime()+downloadFileTiemout, downloadFileTimer);
}

void NoFreeNode::handleTimerEvent( cMessage *msg )
{
    // Es hora de descargarse un archivo de alguien.
    if(msg == downloadFileTimer){
        fileRequest();
    }
    // He pedido un archivo y no me lo han dado, pongo mala reputación.
    else if(msg == fileRequestTimer){
        if(nodeRequested != -1){
            nodeMap[nodeRequested].acceptedRequest--;
            nodeRequested=-1;
        }
    }
    // Ha expirado el tiempo para recibir reputación, decidir si se envía o no.
    else if(msg == reputationRequestTimer){
        reputationRequest();
    }
}

void NoFreeNode::handleMessage( cMessage *msg )
{
    // Se hace cast al tipo de mensaje que heredan todos los demás
    NoFreeMessage *auxmsg = check_and_cast<NoFreeMessage *>(msg);
    // Según su tipo se hace casting al tipo adecuado y se pasa a la función.
    switch(auxmsg->getMessageTipe()){
        case FILE_REQUEST:
        {
            FileRequest *auxmsg = check_and_cast<FileRequest *>(msg);
            handleFileRequest(auxmsg);
            break;
        }
        case REPUTATION_REQUEST:
        {
            ReputationRequest *auxmsg = check_and_cast<ReputationRequest *>(msg);
            handleReputationRequest(auxmsg);
            break;
        }
        case FILE_RESPONSE:
        {
            File *auxmsg = check_and_cast<File *>(msg);
            handleFileResponse(auxmsg);
            break;
        }
        case REPUTATION_RESPONSE:
        {
            Reputation *auxmsg = check_and_cast<Reputation *>(msg);
            handleReputationResponse(auxmsg);
        }
    }
}

void NoFreeNode::handleFileRequest( FileRequest *msg )
{
    // Borra la lista de nodos de los que se ha recibido reputación.
    nodeContributed.clear();
    // Mira a quién estamos sirviendo.
    nodeServed = msg->getSourceNodeId();
    nodeServedGate = msg->getArrivalGate()->getIndex();
    // Si ya tenemos almacenada reputación de este nodo la usamos.
    if(nodeMap.find(nodeServed) != nodeMap.end()){
        tempReputation = nodeMap[nodeServed];
    }
    // Si no se borra la que se tenía, que sería de otro nodo.
    else{
        tempReputation.totalRequest    = 0;
        tempReputation.acceptedRequest = 0;
    }
    // Crea un mensaje ReputationRequest para el nodo que pide.
    ReputationRequest *rrmsg = new ReputationRequest("ReputationRequest");
    rrmsg->setSourceNodeId(getIndex());
    rrmsg->setTargetNodeId(nodeServed);
    // Reenvía copias del ReputationRequest por todas las salidas.
    for(int i=0; i<gateSize("controlGate$o"); i++){
        send(rrmsg->dup(),"controlGate$o", i);
    }
    // Borra el mensaje.
    cancelAndDelete(msg);
}

void NoFreeNode::handleFileResponse( File *msg )
{
    // Si aún no ha vencido el temporizador avisa de que ha recibido para que
    // no le quiten la reputación.
    if(nodeRequested != -1){
        nodeRequested = -1;
    }
    // Borra el mensaje.
    cancelAndDelete(msg);
}

void NoFreeNode::handleReputationRequest( ReputationRequest *msg )
{
    // Si ya tenemos reputación de este nodo la enviamos.
    int targetNode = msg->getTargetNodeId();
    if(nodeMap.find(targetNode) != nodeMap.end()){
        // Crea un mensaje.
        Reputation *rmsg = new Reputation("Reputation");
        rmsg->setTargetNodeId(msg->getTargetNodeId());
        rmsg->setSourceNodeId(getIndex());
        rmsg->setDestinationNodeId(msg->getSourceNodeId());
        rmsg->setTotalRequests(nodeMap[targetNode].totalRequest);
        rmsg->setAcceptedRequests(nodeMap[targetNode].acceptedRequest);
        // La reenvía por la puerta que llegó.
        send(rmsg,"controlGate$o", msg->getArrivalGate()->getIndex());
    }
    // Y se la pedimos a los demás nodos de la red.
    for(int i=0; i<gateSize("controlGate$o"); i++){
        if(msg->getArrivalGate()->getIndex() != i){
            send(msg->dup(),"controlGate$o", i);
        }
    }
    // Borro el mensaje original, que para eso se enviaron duplicados.
    cancelAndDelete(msg);
}

void NoFreeNode::handleReputationResponse( Reputation *msg )
{
    // Si el mensaje de reputación es del nodo que he preguntado, y me lo mandaban a mi.
    if((msg->getTargetNodeId() == nodeServed) && (msg->getDestinationNodeId() == getIndex())){
        // Si aún no tengo almacenada la opinión de ese nodo me la quedo.
        if(nodeContributed.find(nodeServed) != nodeContributed.end()){
            tempReputation.totalRequest += msg->getTotalRequests();
            tempReputation.acceptedRequest += msg->getAcceptedRequests();
            // Añado el nodo a la lista de los que han contribuido para no coger más.
            nodeContributed.insert(msg->getSourceNodeId());
        }
    }
    // Si no era para mi lo reenvío por todas las salidas menos la que llegó.
    else{
        for(int i=0; i<gateSize("controlGate$o"); i++){
            if(msg->getArrivalGate()->getIndex() != i){
                send(msg->dup(),"controlGate$o", i);
            }
        }
    }
    // Borro el mensaje original, que para eso se enviaron duplicados.
    cancelAndDelete(msg);
}

void NoFreeNode::reputationRequest( )
{
    // Decide si el nodo al que servir es digno de ser servido.
    double rate = (double)tempReputation.acceptedRequest / (double)tempReputation.totalRequest;
    // Si es un buen peer se le da el archivo.
    if(rate >= requiredShareRate){
        // Estos campos no son necesarios, pero podría implementarse un factory que lo hiciese por mi.
        File *fmsg = new File("File");
        fmsg->setSourceNodeId(getIndex());
        fmsg->setDestinationNodeId(nodeServed);
        send(fmsg,"dataGate$o", nodeServedGate);
    }
    // Ya se ha decidido si se sirve o no y el nodo queda libre para servir a otra persona.
    nodeServed = -1;
}

void NoFreeNode::finishApp( )
{
    // TODO
}
