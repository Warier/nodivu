//! External process ownership and shared-memory transport; service thread only.
mod broker;
mod job;
mod mapping;
pub use broker::Broker;
