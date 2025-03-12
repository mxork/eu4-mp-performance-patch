#![warn(unused_extern_crates)]
// #![allow(unused)]

// std
use std::net::SocketAddr;
use std::sync::Arc;

// concurrent map
use dashmap::{DashMap as DMap, DashSet as DSet};
use dashmap::mapref::one::Ref as MapRef;

// data
use arcstr::ArcStr as Str;
use uuid::Uuid;
use bytes::Bytes;
use bytes::buf::Buf;
use serde_json as json;
type Rslt<T> = Result<T, anyhow::Error>;
use anyhow::anyhow;

// log & error
use log::{debug, error, info, trace, warn};

// jwt stuff
use hmac::{Hmac, Mac};
use sha2::Sha256;
use jwt::{SignWithKey, VerifyWithKey};

// async stuff
use tokio::sync::mpsc;
use futures::sink::SinkExt;
use futures::stream::StreamExt;
use http_body_util::{Full, BodyExt};
use hyper::body::Incoming;
use hyper::{Request, Response, StatusCode};
use hyper_tungstenite::HyperWebsocket;
use hyper_tungstenite::tungstenite::Message;
use hyper_util::rt::TokioIo;

// nakama protobufs
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

// the big mutable state objects
struct State {
    peers: PeerMap,
    matches: MatchMap,
}

type PeerMap = DMap<Str, PeerMapData>;
#[allow(dead_code)]
struct PeerMapData {
    id: Str,
    address: SocketAddr,
    sender: mpsc::Sender<Message>,
    match_id: Option<Str>,
    username: Str,
}

type MatchMap = DMap<Str, MatchMapData>;
#[allow(dead_code)]
struct MatchMapData {
    id: Str,
    host: Str,
    peers: DSet<Str>,
    label: Str,
}

// single endpoint either directly responds to an authenticate call over http,
// or expects to receive a websocket connection
#[tokio::main]
async fn main() -> anyhow::Result<()> {
    env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("info")).init();
    let state = Arc::new(State{
        peers: PeerMap::new(),
        matches: MatchMap::new(),
    });

    let addr_str = std::env::var("RUNK_LISTEN_ADDR").unwrap_or("0.0.0.0:7350".to_owned());
    let addr: std::net::SocketAddr = addr_str.parse()?;
    let listener = tokio::net::TcpListener::bind(&addr).await?;
    info!("listening on {addr}");

    let mut http = hyper::server::conn::http1::Builder::new();
    http.keep_alive(true);

    loop {
        let state = state.clone();
        let (stream, addr) = listener.accept().await?;
        let service = move |request| {
            let state = state.clone();
            let addr = addr.clone();
            async move { handle_request(state, addr, request).await }
        };
        let connection = http
            .serve_connection(TokioIo::new(stream), hyper::service::service_fn(service))
            .with_upgrades();
        tokio::spawn(async move {
            if let Err(err) = connection.await {
                error!("error serving connection: {err:?}");
            }
        });
    }
}

// dispatch to either handle_authenticate or handle_websocket
async fn handle_request(state: Arc<State>, addr: SocketAddr, mut request: Request<Incoming>)
    ->  Rslt<Response<Full<Bytes>>> {
    debug!("handle request");
    if hyper_tungstenite::is_upgrade_request(&request) {
        info!("websocket connection from {}", addr);
        let token = request.uri().query()
            .map(|v| {
                url::form_urlencoded::parse(v.as_bytes())
                    .find(|(k,_v)| k == "token")
                    .map(|(_k,v)| v.to_string())
            })
            .unwrap()
            .unwrap()
        ;
        debug!("websocket connect with token {} {}", addr, token);

        let token: jwt::Token<jwt::Header, json::Map<String, json::Value>, jwt::Unsigned> = {
            if std::env::var("FORCE_JWT_VERIFY").is_ok() {
                let key: Hmac<Sha256> = Hmac::new_from_slice(SECRET)?;
                let token: jwt::Token<jwt::Header, json::Map<String, json::Value>, _> = token.verify_with_key(&key).expect("jwt failed verification");
                token.remove_signature()
            } else {
                jwt::Token::parse_unverified(&token).unwrap().remove_signature()
            }
        };

        let (response, websocket) = hyper_tungstenite::upgrade(&mut request, None).expect("fail to upgrade");
        tokio::spawn(async move {
            if let Err(e) = handle_websocket(state, addr, token, websocket).await {
                error!("error in websocket connection: {e}");
            }
        });
        Ok(response)
    } else {
        match request.uri().path() {
            "/v2/account/authenticate/custom" => {
                debug!("authenticate custom request");
                let response = handle_authenticate(request).await;
                Ok(response)
            },
            _ => {
                debug!("bad request");
                let response = Response::builder()
                    .status(StatusCode::BAD_REQUEST)
                    .body(Full::default())
                    .unwrap();
                Ok(response)
            },
        }
    }
}

// :todo add http basic auth
// :note I don't really know what Full is doing, I'm cargo culting
static SECRET: &[u8] = b"static.secret.6e686bff502ba7a2";
async fn handle_authenticate(request: Request<Incoming>) -> Response<Full<Bytes>> {
    // payload
    // {
    //   "id": "376625ef-a4c7-4b6e-b37f-a999372c84ea",
    //   "vars": {
    //     "game_name": "eu4",
    //     "session_token": "4fc3ea56-6481-4ebc-b20c-541d1abd0547"
    //   }
    // }
    //
    // jwt
    //{
    // "tid": "16be73a2-2ead-4ac1-8f06-0d4cb885d83e",
    // "uid": "805df77f-c429-40a1-a5e0-8cf9838e116c",
    // "usn": "fvayXjcFKp",
    // "vrs": {
    //   "game_name": "eu4",
    //   "session_token": "4fc3ea56-6481-4ebc-b20c-541d1abd0547"
    // },
    // "exp": 1740986553,
    // "iat": 1740979353
    // }
    let body = request.into_body().collect().await.unwrap().aggregate().reader();

    // :note type api.AccountCustom but doesn't implement DeserializeOwned
    let payload: api::AccountCustom = json::from_reader(body).expect("reading authenticate payload");

    // this mirrors the naka generator, even though it's probably arbitrary
    let vrs = payload.vars;
    let usn = random_string::generate(10, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
    let now = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_secs();
    let claims = json::json!({
        "tid": Uuid::new_v4().to_string(),
        "uid": Uuid::new_v4().to_string(),
        "usn": usn,
        "vrs": vrs,
        "exp": now + 3600*8,
        "iat": now,
    });

    let headers = jwt::Header{
        algorithm: jwt::algorithm::AlgorithmType::Hs256,
        type_: Some(jwt::header::HeaderType::JsonWebToken),
        ..jwt::Header::default()
    };
    let key: Hmac<Sha256> = Hmac::new_from_slice(SECRET).unwrap();
    let token = jwt::Token::new(headers, claims).sign_with_key(&key).unwrap().as_str().to_string();
    let refresh_token = format!("{}_44", token); // shiiit, man. I don't make the rules.
    let response = Response::builder()
        .status(200)
        .header("content-type", "application/json")
        // :note is also type api.Session
        .body(Full::new(Bytes::from(json::json!({"token": token, "refresh_token": refresh_token}).to_string())))
        .unwrap();

    return response;
}

// add the connected client to the big mutable state, spawn
// two tasks to receive and send messages.
// running them independently isn't really necessary, but whatever.
async fn handle_websocket<S>(state: Arc<State>, addr: SocketAddr, token: jwt::Token<jwt::Header, json::Map<String, json::Value>, S>, websocket: HyperWebsocket) -> Result<(), anyhow::Error> {
    // completes on handshake complete
    let websocket = websocket.await?;
    let (mut outgoing, mut incoming) = websocket.split();

    // initialization
    let (tx, mut rx) = mpsc::channel(32);
    let tx_for_status_event = tx.clone();
    let claims = token.claims();
    let id = arcstr::format!("{}", claims.get("uid").unwrap().as_str().unwrap()); // :todo
    let username = arcstr::format!("{}", claims.get("usn").unwrap().as_str().unwrap()); // :todo
    let peerdata = PeerMapData {
        id: id.clone(),
        address: addr,
        sender: tx,
        match_id: None,
        username: username.clone(),
    };
    state.peers.insert(id.clone(), peerdata);

    info!("peer joined {} {} {}", addr, id, username);

    // :hack onstatuspresence
    //       this really shouldn't be necessary....
    //       unless we're finding out what our name should be
    let envelope = rt::Envelope{
        message: Some(rt::envelope::Message::StatusPresenceEvent(
            rt::StatusPresenceEvent{
                joins: vec![user_presence_from(&id, &username)],
                ..rt::StatusPresenceEvent::default()
            }
          )),
        ..rt::Envelope::default()
    };
    let message = Message::binary(envelope.encode_to_vec());
    tokio::spawn(async move {
        tx_for_status_event.send(message).await
    });
    // :endhack


    // puller
    {
        let handler = PeerHandler(state.clone(), id.clone());
        tokio::spawn(async move {
            while let Some(Ok(message)) = incoming.next().await {
                if let Err(e) = handler.handle_message(message).await {
                    error!("error: {:?}", e);
                    break;
                }
            }
            handler.on_disconnect();
        });
    }

    // pusher
    {
        let handler = PeerHandler(state.clone(), id.clone());
        tokio::spawn(async move {
            while let Some(message) = rx.recv().await {
                if let Err(e) = outgoing.send(message).await {
                    error!("error: {:?}", e);
                    break;
                }
            }
            handler.on_disconnect();
        });
    }

    Ok(())
}

fn user_presence_from(id: &Str, username: &Str) -> rt::UserPresence {
    rt::UserPresence{
        user_id: id.to_string(),
        session_id: id.to_string(),
        username: username.to_string(), //eeeeennnn.
        ..rt::UserPresence::default()
    }
}

fn generate_match_id() -> Str {
    let id = uuid::Uuid::new_v4();
    arcstr::format!("{}.nakama-0", id)
}

fn get_peer<'a>(state: &'a State, id: &Str) -> Rslt<MapRef<'a, Str, PeerMapData>> {
    state.peers.get(id).ok_or(anyhow!("peer does not exist"))
}

fn get_match<'a>(state: &'a State, id: &Str) -> Rslt<MapRef<'a, Str, MatchMapData>> {
    state.matches.get(id).ok_or(anyhow!("match does not exist"))
}


// the big thing that handles most of the things.
// careful with scoping on mutable borrows from
// state.peers and state.matches; you can deadlock.
//
// see handle_message for the entrypoint.
#[derive(Clone)]
struct PeerHandler(Arc<State>, Str);
impl PeerHandler {
    async fn send_to(message: rt::envelope::Message, senders: impl Iterator<Item=mpsc::Sender<Message>>) {
        let envelope = rt::Envelope{
            message: Some(message),
            ..rt::Envelope::default()
        };
        let message = Message::binary(envelope.encode_to_vec());
        Self::send_raw_to(message, senders).await;
        // :todo could add an external counter to assert ordering
    }

    async fn send_raw_to(message: Message, senders: impl Iterator<Item=mpsc::Sender<Message>>) {
        // :todo
        // let futures = (0..4).map(|v| async move { v + 1 });
        // assert_eq!(
        //     join_all(futures).await,
        //     vec![1, 2, 3, 4]
        // );
        for tx in senders {
            let message = message.clone(); // should be shallow
            // :todo should spawn?
            tx.send(message).await.unwrap()
        }
    }

    async fn send_to_presences(&self, message: rt::envelope::Message, presences: Vec<rt::UserPresence>) {
        trace!("sending out to everyone *except* sender {:?}", message);
        // :todo could lift this
        let PeerHandler(state, our_id) = self;
        let us = state.peers.get(our_id).expect("we don't exist");
        let match_id = us.match_id.clone().expect("not in a match");
        let the_match = get_match(&state, &match_id).expect("match does not exist");

        let senders = the_match.peers.iter().filter_map(
            |e| {
                let id = e.key();
                if id == our_id {
                    return None;
                }
                if !presences.iter().any(|p| e.key() == &p.session_id) {
                    return None;
                }
                if let Some(peer) = state.peers.get(id) {
                    return Some(peer.sender.clone());
                } else {
                    // disconnected, presumably
                    return None;
                }
            }
        );

        PeerHandler::send_to(message, senders).await;
    }

    async fn send_everybody_else(&self, message: rt::envelope::Message) {
        trace!("sending out to everyone *except* sender {:?}", message);
        // :todo could lift this
        let PeerHandler(state, our_id) = self;
        let us = state.peers.get(our_id).expect("we don't exist");
        let match_id = us.match_id.clone().expect("not in a match");
        let the_match = get_match(&state, &match_id).expect("match does not exist");

        let senders = the_match.peers.iter().filter_map(
            |e| {
                let id = e.key();
                if id == our_id {
                    return None;
                }
                if let Some(peer) = state.peers.get(id) {
                    return Some(peer.sender.clone());
                } else {
                    // disconnected, presumably
                    return None;
                }
            }
        );

        PeerHandler::send_to(message, senders).await;
    }

    async fn send_everybody(&self, message: rt::envelope::Message) {
        let PeerHandler(state, our_id) = self;
        let us = state.peers.get(our_id).expect("we don't exist");
        let match_id = us.match_id.clone().expect("not in a match");
        let the_match = get_match(&state, &match_id).expect("match does not exist");

        let senders = the_match.peers.iter().filter_map(
            |e| {
                let id = e.key();
                if let Some(peer) = state.peers.get(id) {
                    return Some(peer.sender.clone());
                } else {
                    // disconnected, presumably
                    return None;
                }
            }
        );

        PeerHandler::send_to(message, senders).await;
    }

    async fn reply(&self, our_id: &Str, cid: String, message: rt::envelope::Message) {
        let tx = {
            &self.0.peers.get(our_id).expect("no such peer").sender
        };

        let envelope = rt::Envelope{
            cid,
            message: Some(message),
            ..rt::Envelope::default()
        };
        let message = Message::binary(envelope.encode_to_vec());
        let _ = tx.send(message).await;
    }

    // :todo add a channel to signal disconnects up to handle_websocket
    //       and generally divvy up error handling.
    fn on_disconnect(&self) {
        let PeerHandler(state, our_id) = self;
        info!("peer disconnected {}", our_id);
        // this is a mess because we need to cascade remove things,
        // but also make sure to drop references before trying to remove.
        {
            let us = state.peers.get(our_id);
            match us {
                None => {
                    return;
                },
                Some(p) => {
                    if let Some(match_id) = &p.match_id {
                        let match_empty = {
                            match state.matches.get(match_id) {
                                None => false,
                                Some(the_match) => {
                                    the_match.peers.remove(our_id);
                                    the_match.peers.is_empty()
                                }
                            }
                        };

                        if match_empty {
                            state.matches.remove(match_id);
                        }
                    }
                },
            }
        }

        state.peers.remove(our_id);
    }

    async fn handle_message(&self, message: Message) -> Rslt<()> {
        let PeerHandler(state, our_id) = self;
        // :todo Bytes
        let data = message.into_data();
        // :note this crashes on match leave? not done with
        //       DecodeError description: "invalid wire type value: 6
        //
        //       maybe it's supposed to be text
        let envelope = rt::Envelope::decode(data)?;
        debug!("received {:?}", envelope);

        // only using this for responses, could move in
        match envelope.message {
            None => eprintln!("empty message"),
            Some(rt::envelope::Message::Ping(_)) => {
                debug!("ping");
                self.reply(our_id, envelope.cid, rt::envelope::Message::Pong(rt::Pong::default())).await;
            }
            Some(rt::envelope::Message::Pong(_)) => {
                debug!("pong");
            }

            // :todo hotjoining requires suspending and waiting for confirmation
            //       from host.
            Some(rt::envelope::Message::MatchJoin(join)) => {
                if let Some(rt::match_join::Id::MatchId(match_id)) = join.id {
                    // :todo more validation
                    let match_id = Str::from(match_id);
                    info!("match join attempt peer {} to match {}", our_id, match_id);
                    #[allow(unused_assignments)]
                    let mut was_already_joined = false;
                    let existing_presences: Vec<_> = {
                        let mut us = state.peers.get_mut(our_id).ok_or(anyhow!("abort: we do not exist in peers"))?;
                        let the_match = state.matches.get_mut(&match_id).ok_or(anyhow!("match does not exist"))?;
                        // :note I'm not sure what the official behavior is on repeat match joins
                        was_already_joined = the_match.peers.contains(our_id);

                        let existing_presences = the_match.peers.iter()
                            .map(|e| user_presence_from(e.key(), &us.username))
                            .collect();

                        the_match.peers.insert(our_id.clone());
                        us.match_id = Some(match_id.clone());
                        existing_presences
                    };

                    let reply =
                        rt::envelope::Message::Match(
                            rt::Match{
                                match_id: match_id.to_string(),
                                authoritative: true,
                                label: Some("bWF0Y2g=".to_string()), // :note official servers returns a whole json object
                                size: existing_presences.len() as i32,
                                presences: existing_presences,
                                self_: Some(user_presence_from(our_id, &get_peer(&state, &our_id)?.username)), // why not, its optional
                                ..rt::Match::default()
                            });
                    self.reply(our_id, envelope.cid, reply).await;

                    if !was_already_joined {
                        self.send_everybody(
                            rt::envelope::Message::MatchPresenceEvent(
                                rt::MatchPresenceEvent{
                                    match_id: match_id.to_string(),
                                    joins: vec![user_presence_from(our_id, &get_peer(&state, &our_id)?.username)],
                                    ..rt::MatchPresenceEvent::default()
                                }
                            ),
                        ).await;
                    }
                } else {
                    panic!("unexpected match join with token");
                }
            }
            // :note bathcing doesn't matter
            // :todo only one match right now
            Some(rt::envelope::Message::MatchLeave(leave)) => {
                let rt::MatchLeave{match_id} = leave;
                let match_id = Str::from(match_id);

                {
                info!("match leave peer {} from match {}", our_id, match_id);
                let the_match = get_match(&state, &match_id)?;
                the_match.peers.remove(our_id);
                }

                // :todo cleanup
                // :note nakama doesn't have a MatchEnded event
                // :note this is tricky, since we need to actually ensure
                //       the sends have complete before deletion.
                // :rewrite
                // if the_match.peers.is_empty() {
                //     state.matches.remove(match_id);
                //     return;
                // }

                self.send_everybody_else(
                    rt::envelope::Message::MatchPresenceEvent(
                        rt::MatchPresenceEvent{
                            match_id: match_id.to_string(),
                            leaves: vec![user_presence_from(our_id, &get_peer(&state, &our_id)?.username)],
                            ..rt::MatchPresenceEvent::default()
                        }),
                ).await;
            }

            Some(rt::envelope::Message::MatchDataSend(data)) => {
                // :todo currently ignoring the specified presences
                debug!("match data");
                let rt::MatchDataSend {match_id, op_code, data, presences, reliable} = data;
                let message =
                    rt::envelope::Message::MatchData(
                        rt::MatchData{
                            match_id: match_id.clone(),
                            presence: Some(user_presence_from(our_id, &get_peer(&state, &our_id)?.username)),
                            op_code,
                            reliable,
                            data,
                            ..rt::MatchData::default()
                        });
                // :note I'm like 90% that eu4 doesn't ever specify a list
                //       of presences. whether that's because its just aggressively
                //       broadcasting everything or the server determines the correct
                //       recipients, I'm unsure.
                if presences.is_empty() {
                    self.send_everybody_else(message).await;
                } else {
                    self.send_to_presences(message, presences).await;
                }
            }

            Some(rt::envelope::Message::Rpc(api::Rpc{id, payload, http_key: _})) => {
                info!("rpc from peer {} {} {}", our_id, id, payload);
                let cid = envelope.cid;
                // :todo ignoring http_key
                match id.as_str() {
                    "create_jomini_match" => {
                        let payload = json::from_str::<json::Map<String, json::Value>>(&payload)
                            .expect("create jomini match payload")
                            ;

                        let label = payload.get("server_name")
                            .expect("no .server_name")
                            .as_str()
                            .expect(".server_name not string");

                        let match_id = generate_match_id();
                        state.matches.insert(
                            match_id.clone(),
                            MatchMapData{
                                id: match_id.clone(),
                                host: our_id.clone(),
                                peers: DSet::new(),
                                label: label.into(),
                            },
                        );

                        let message =
                            rt::envelope::Message::Rpc(
                                api::Rpc{
                                    id,
                                    payload: json::json!({
                                        "match_id": match_id, // arb
                                    }).to_string(),
                                    ..api::Rpc::default()
                                })
                        ;
                        self.reply(our_id, cid, message).await;
                    },
                    "list_matches" => {
                        let _payload: json::Value  = json::from_str(&payload).expect("create jomini match payload");
                        let message =
                            rt::envelope::Message::Rpc(
                                api::Rpc{
                                    id,
                                    payload: json::json!({
                                        "matches":
                                            state.matches.iter().map(|e| {
                                                let _id = e.key();
                                                let the_match = e.value();
                                                (
                                                    e.key().to_string(),
                                                    json::json!({
                                                        "cross_play": true,
                                                        "description": "AA==",
                                                        "game_name": "eu4",
                                                        "has_password": false, // :todo
                                                        // this doesn't matter.
                                                        "host_user_id": the_match.host.clone(),
                                                        "open": true,
                                                        "password": "",
                                                        "platform": "steam",
                                                        "presence_count": the_match.peers.len(),
                                                        "product": "europa_universalis_iv",
                                                        "public": true,
                                                        "server_name": the_match.label.clone(),
                                                        "status": "lobby", // :todo
                                                        "tags": [],
                                                        "version": "EU4 v1.37.5.0 Inca (491d)" // erf
                                                    }),
                                                )
                                            })
                                            .collect::<json::Map<String, json::Value>>()
                                    }).to_string(),
                                    ..api::Rpc::default()
                                })
                        ;
                        self.reply(our_id, cid, message).await;
                    },
                    _ => {
                        warn!("received unexpected rpc call: {}", id);
                    }
                }
            }
            _ => warn!("unhandled message"),
        }

        Ok(())
    }
}
