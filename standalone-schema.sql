CREATE TABLE inventory_folders (user_id varchar(36), folder_id varchar(36), parent_id varchar(36), name text, version integer, asset_type integer);
CREATE TABLE inventory_items (user_id varchar(36), item_id varchar(36), folder_id varchar(36), name text, description text, creator text, inv_type integer, asset_type integer, asset_id varchar(36), base_perms integer, current_perms integer, next_perms integer, group_perms integer, everyone_perms integer, group_id varchar(36), group_owned integer, sale_price integer, sale_type integer, flags integer, creation_date integer);
CREATE TABLE users (first_name varchar(30) not null, last_name varchar(30) not null, id varchar(36) primary key not null, session_id varchar(36), passwd_salt varchar(10), passwd_sha256 varchar(64), time_created integer not null, home_region varchar(36), home_pos text, home_look_at text, last_region varchar(36), last_pos text, last_look_at text);
CREATE UNIQUE INDEX folder_index ON inventory_folders(user_id, folder_id);
CREATE UNIQUE INDEX folder_parent_index ON inventory_folders(user_id, parent_id);
CREATE UNIQUE INDEX item_folder_index ON inventory_items(user_id, folder_id);
CREATE UNIQUE INDEX item_index ON inventory_items(user_id, item_id);
