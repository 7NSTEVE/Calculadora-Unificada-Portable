do
	local buttons = {}
	local function generate(px, py, w, h, nx, ny, sx, sy, code)
		local cp = 1
		for iy = 0, ny - 1 do
			for ix = 0, nx - 1 do
				table.insert(buttons, {px + ix*sx, py + iy*sy, w, h, code[cp], code[cp+1]})
				cp = cp + 2
			end
		end
	end
	-- Refer to https://wiki.libsdl.org/SDL_Keycode for key names.
	-- Official Casio fx-991CW studio master faceplate (992x2082 body, status strip at y=2096).
	emu:model({
		model_name = "fx-991CW",
		interface_image_path = "interface.png",
		rom_path = "rom.bin",
		hardware_id = 5,
		real_hardware = 0,
		csr_mask = 0x000F,
		pd_value = 0x00,
		rsd_interface = {0, 0, 992, 2082, 0, 0},
		rsd_pixel = { 112, 358,  4,  4,  112, 358},
		rsd_s     = { 112, 2096, 28, 28, 112, 322},
		rsd_math  = { 152, 2096, 52, 28, 152, 322},
		rsd_d     = { 216, 2096, 28, 28, 216, 322},
		rsd_r     = { 252, 2096, 28, 28, 252, 322},
		rsd_g     = { 284, 2096, 28, 28, 284, 322},
		rsd_fix   = { 328, 2096, 42, 28, 328, 322},
		rsd_sci   = { 384, 2096, 38, 28, 384, 322},
		rsd_e     = { 444, 2096, 18, 28, 444, 322},
		rsd_cmplx = { 478, 2096, 18, 28, 478, 322},
		rsd_angle = { 500, 2096, 28, 28, 500, 322},
		rsd_wdown = { 534, 2096, 26, 28, 534, 322},
		rsd_verify = { 572, 2096, 28, 28, 572, 322},
		rsd_left  = { 614, 2096, 20, 28, 614, 322},
		rsd_down  = { 644, 2096, 24, 28, 644, 322},
		rsd_up    = { 672, 2096, 24, 28, 672, 322},
		rsd_right = { 706, 2096, 20, 28, 706, 322},
		rsd_pause = { 744, 2096, 32, 28, 744, 322},
		rsd_sun   = { 784, 2096, 32, 28, 784, 322},
		ink_colour = {20, 36, 58},
		button_map = buttons
	})
	generate(  78, 1432, 122, 122, 5, 4, 179, 150, {
		0x02, '7', 0x12, '8', 0x22, '9', 0x32, 'Backspace', 0x42, 'Space',
		0x01, '4', 0x11, '5', 0x21, '6', 0x31, '' , 0x41, '/',
		0x00, '1', 0x10, '2', 0x20, '3', 0x30, '=', 0x40, '-',
		0x64, '0', 0x63, '.', 0x62, 'E', 0x61, '' , 0x60, 'Return',
	})
	generate(  85, 1175,  94,  94, 6, 2, 146, 128, {
		0x04, '', 0x14, '', 0x24, '', 0x34, '', 0x44, '', 0x54, '',
		0x03, '', 0x13, '', 0x23, '', 0x33, '', 0x43, '', 0x53, '',
	})
	generate(  84,  891,  96,  96, 6, 1, 146,   0, {
		0x06, '', 0x16, '', 0x26, '', 0x36, '', 0x46, '', 0x56, '',
	})
	generate(  84, 1039,  96,  96, 3, 1, 146,   0, {0x05, 'F5', 0x15, 'F6', 0x25, '',})
	generate( 668, 1039,  96,  96, 2, 1, 146,   0, {0x45, 'F7', 0x55, 'F8',})
	generate( 522, 1017,  96,  96, 1, 1,   0,   0, {0x35, 'F1',})
	generate( 230,  749,  96,  96, 2, 1, 292,   0, {0x17, 'F3', 0x37, 'F4',})
	generate( 814,  756,  96, 110, 1, 1,   0,   0, {0x57, '',})
	generate(  87,  750,  90,  90, 1, 1,   0,   0, {0xFF, 'Left'})
end
