#![allow(unused)]
use prost::Message as _;
use nakama::api as api;
use nakama::realtime as rt;
pub mod nakama {
    pub mod api {
        include!(concat!(env!("OUT_DIR"), "/nakama.api.rs"));
    }
    pub mod realtime {
        include!(concat!(env!("OUT_DIR"), "/nakama.realtime.rs"));
    }
}

use serde_json as json;
use tokio::sync::mpsc;

use tokio::time::{sleep, Duration};
use hyper_tungstenite::tungstenite as tung;

use futures::prelude::*;
use futures::stream::{SplitSink, SplitStream};
use futures_util::{future, pin_mut, StreamExt};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio_tungstenite::{connect_async, tungstenite::protocol::Message, WebSocketStream};

async fn send(
    write: &mut SplitSink<WebSocketStream<tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>>, tokio_tungstenite::tungstenite::Message>,
    read: &mut  SplitStream<WebSocketStream<tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>>>,
    message: rt::envelope::Message)
{
    eprintln!("send: {:?}", message);
    let envelope = rt::Envelope{
        message: Some(message),
        ..rt::Envelope::default()
    };
    write.send(Message::binary(envelope.encode_to_vec())).await;
}

async fn send_and_recv(
    cid: &mut i32,
    write: &mut SplitSink<WebSocketStream<tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>>, tokio_tungstenite::tungstenite::Message>,
    read: &mut  SplitStream<WebSocketStream<tokio_tungstenite::MaybeTlsStream<tokio::net::TcpStream>>>,
    message: rt::envelope::Message)
-> rt::envelope::Message {
    *cid += 1;
    eprintln!("send: {} {:?}", *cid, message);
    let envelope = rt::Envelope{
        cid: cid.to_string(),
        message: Some(message),
        ..rt::Envelope::default()
    };
    write.send(Message::binary(envelope.encode_to_vec())).await;
    let response = read.next().await.unwrap();
    let data = response.unwrap().into_data();
    let envelope = rt::Envelope::decode(data).unwrap();
    eprintln!("received {:?}", envelope);
    eprintln!("");
    return envelope.message.unwrap();
}

use serde::{Serialize, Deserialize};
use prost::{Message as _};
#[tokio::main]
async fn main() {
    // let url = env::args().nth(1).unwrap_or_else(|| panic!("this program requires at least one argument"));
    let url = arcstr::ArcStr::from("ws://localhost:7350/ws");
    let url2 = url.clone();
    let (created_send, mut created_recv) = mpsc::channel(1);
    let (joined_send, mut joined_recv) = mpsc::channel(1);

    // let stdin_to_ws = stdin_rx.map(Ok).forward(write);
    let driver = tokio::spawn(async move {
        let client = reqwest::Client::new();
        let auth_response = client.post("http://localhost:7350/v2/account/authenticate/custom?create=true&username=")
            .basic_auth("defaultkey", Some(""))
            .json(&json::json!({"id": uuid::Uuid::new_v4().to_string(), "vars": {"game": "eu4", "session_token": uuid::Uuid::new_v4().to_string()}}))
            .send()
            .await
            .unwrap();
        let auth_response = auth_response
            // :note type api::Session but field create is not mandatory
            // .json::<json::Map<String,json::Value>>()
            .text()
            .await
            .unwrap()
            ;
        let auth_response: json::Map<String, json::Value> = json::from_str(&auth_response).unwrap();
        let token = auth_response.get("token").unwrap().as_str().unwrap();
        let url = format!("{}?token={}&status=true&format=protobuf", url, token);
        eprintln!("trying to connect to {}", url);
        let (ws_stream, _) = connect_async(url).await.expect("Failed to connect");
        eprintln!("driver WebSocket handshake has been successfully completed");

        let (mut write, mut read) = ws_stream.split();
        let mut cid = 0;

        read.next().await; // drain the status event

        let message =
            rt::envelope::Message::Rpc(
                api::Rpc{
                    id: "create_jomini_match".to_string(),
                    payload: json::json!({"server_name": "tickler"}).to_string(),
                    ..api::Rpc::default()
                }
            );
        let response = send_and_recv(&mut cid, &mut write, &mut read, message).await;
        let match_id = {
            if let rt::envelope::Message::Rpc(api::Rpc{id, payload, http_key}) = response {
                json::from_str::<json::Value>(&payload).unwrap().as_object().unwrap().get("match_id").unwrap().as_str().unwrap().to_owned()
            } else {
                panic!("no match id")
            }
        };
        created_send.send(arcstr::ArcStr::from(&match_id)).await;

        let message =
            rt::envelope::Message::Rpc(
                api::Rpc{
                    id: "list_matches".to_string(),
                    payload: json::json!({}).to_string(),
                    ..api::Rpc::default()
                }
            );
        send_and_recv(&mut cid, &mut write, &mut read, message).await;

        let message =
            rt::envelope::Message::MatchJoin(
                rt::MatchJoin{
                    id: Some(
                            rt::match_join::Id::MatchId(
                                match_id.clone(),
                            )),
                            ..rt::MatchJoin::default()
                }
            );
        send_and_recv(&mut cid, &mut write, &mut read, message).await;

        while (cid < 5) {
            let message = rt::envelope::Message::Ping(rt::Ping::default());
            send_and_recv(&mut cid, &mut write, &mut read, message).await;
            sleep(Duration::from_secs(1)).await;
        }

        joined_recv.recv().await.unwrap();
        let mut data_size = 128;
        while (data_size < 10000) {
            let message =
                rt::envelope::Message::MatchDataSend(
                    rt::MatchDataSend{
                        match_id: match_id.clone(),
                        op_code: 255,
                        data: vec![2; data_size],
                        ..rt::MatchDataSend::default()
                    });
            send(&mut write, &mut read, message).await;
            data_size *= 2;
            sleep(Duration::from_secs(1)).await;
        }

        let message =
            rt::envelope::Message::MatchLeave(
                rt::MatchLeave{
                    match_id: match_id.clone(),
                    ..rt::MatchLeave::default()
                });
        send_and_recv(&mut cid, &mut write, &mut read, message).await;
    });

    let joiner = tokio::spawn(async move {
        let client = reqwest::Client::new();
        let auth_response = client.post("http://localhost:7350/v2/account/authenticate/custom?create=true&username=")
            .basic_auth("defaultkey", Some(""))
            .json(&json::json!({"id": uuid::Uuid::new_v4().to_string(), "vars": {"game": "eu4", "session_token": uuid::Uuid::new_v4().to_string()}}))
            .send()
            .await
            .unwrap();
        let auth_response = auth_response
            // :note type api::Session but field create is not mandatory
            // .json::<json::Map<String,json::Value>>()
            .text()
            .await
            .unwrap()
            ;
        eprintln!("response: {}", auth_response);
        let auth_response: json::Map<String, json::Value> = json::from_str(&auth_response).unwrap();
        let token = auth_response.get("token").unwrap().as_str().unwrap();
        let url = format!("{}?token={}&status=true&format=protobuf", url2, token);
        eprintln!("trying to connect to {}", url);
        let (ws_stream, _) = connect_async(url).await.expect("Failed to connect");

        eprintln!("driver WebSocket handshake has been successfully completed");
        let (mut write, mut read) = ws_stream.split();
        let mut cid = 0;
        let match_id = created_recv.recv().await.unwrap();

        let message =
            rt::envelope::Message::Rpc(
                api::Rpc{
                    id: "list_matches".to_string(),
                    payload: json::json!({}).to_string(),
                    ..api::Rpc::default()
                }
            );
        send_and_recv(&mut cid, &mut write, &mut read, message).await;

        let message =
            rt::envelope::Message::MatchJoin(
                rt::MatchJoin{
                    id: Some(
                            rt::match_join::Id::MatchId(
                                match_id.to_string(),
                            )),
                            ..rt::MatchJoin::default()
                }
            );
        joined_send.send(()).await;
        send_and_recv(&mut cid, &mut write, &mut read, message).await;

        loop {
            match read.next().await {
                None => break,
                Some(Err(e)) => {eprintln!("err {}", e); break;},
                Some(Ok(v)) => {
                    let envelope = rt::Envelope::decode(v.into_data()).unwrap();
                    eprintln!("joiner got match data {:?}", envelope.encoded_len());
                },
            }
        }
    });

    pin_mut!(driver, joiner);
    future::select(driver, joiner).await;
}

// Our helper method which will read data from stdin and send it along the
// sender provided.
async fn read_stdin(tx: futures_channel::mpsc::UnboundedSender<Message>) {
    let mut stdin = tokio::io::stdin();
    loop {
        let mut buf = vec![0; 1024];
        let n = match stdin.read(&mut buf).await {
            Err(_) | Ok(0) => break,
            Ok(n) => n,
        };
        buf.truncate(n);
        tx.unbounded_send(Message::binary(buf)).unwrap();
    }
}
