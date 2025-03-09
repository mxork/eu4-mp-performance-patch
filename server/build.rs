use std::io::Result;
fn main() -> Result<()> {
    let mut config = prost_build::Config::new();
    config.type_attribute("nakama.api.Session", "#[derive(serde::Serialize,serde::Deserialize)]");
    config.type_attribute("nakama.api.AccountCustom", "#[derive(serde::Serialize,serde::Deserialize)]");
    config.compile_protos(
        &[
        "vendor/nakama-common/api/api.proto",
        "vendor/nakama-common/rtapi/realtime.proto",
        ],
        &[
        "vendor/nakama-common",
        "vendor/protobuf/src",
        ],
        )?;
    Ok(())
}
