CREATE TABLE assets (id varchar(36) primary key not null, name text, description text, asset_type int not null, data blob not null);
CREATE TABLE inventory_folders (user_id varchar(36) not null, folder_id varchar(36) not null, parent_id varchar(36), name text not null, version integer not null, asset_type integer not null, PRIMARY KEY(user_id, folder_id), FOREIGN KEY(user_id, parent_id) REFERENCES inventory_folders(user_id, folder_id) ON DELETE CASCADE ON UPDATE RESTRICT);
CREATE TABLE inventory_items (user_id varchar(36) not null, item_id varchar(36) not null, folder_id varchar(36) not null, name text not null, description text not null, creator text, inv_type integer not null, asset_type integer not null, asset_id varchar(36) not null, base_perms integer not null, current_perms integer not null, next_perms integer not null, group_perms integer not null, everyone_perms integer not null, group_id varchar(36) not null, group_owned integer not null, sale_price integer not null default 0, sale_type integer not null default 0, flags integer not null default 0, creation_date integer not null default 0, PRIMARY KEY(user_id, item_id), FOREIGN KEY(user_id, folder_id) REFERENCES inventory_folders(user_id, folder_id) ON DELETE CASCADE ON UPDATE RESTRICT);
CREATE TABLE users (first_name varchar(30) not null, last_name varchar(30) not null, id varchar(36) primary key not null, session_id varchar(36), passwd_salt varchar(10), passwd_sha256 varchar(64), time_created integer not null, home_region varchar(36), home_pos text, home_look_at text, last_region varchar(36), last_pos text, last_look_at text);
CREATE TABLE wearables (user_id varchar(36) NOT NULL, wearable_id int NOT NULL, item_id varchar(36) NOT NULL, asset_id varchar(36) NOT NULL);
CREATE INDEX folder_parent_index ON inventory_folders(user_id, parent_id);
CREATE INDEX item_folder_index ON inventory_items(user_id, folder_id);
CREATE UNIQUE INDEX wearable_id_index ON wearables(user_id, wearable_id);
CREATE INDEX wearable_index ON wearables(user_id);
