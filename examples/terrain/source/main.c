#include <gs.h>

#include "noise1234.h"

// Heightmap Terrain Demo

typedef struct model_t
{
	gs_resource( gs_vertex_buffer ) vbo;
	gs_resource( gs_index_buffer ) ibo;
	u32 vertex_count;
} model_t;

typedef struct color_t
{
	u8 r;
	u8 g;
	u8 b;
	u8 a;
} color_t;

typedef struct terrain_type
{
	f32 height;
	color_t color;
} terrain_type;

// Globals
_global gs_resource( gs_shader ) 			shader = {0};
_global gs_resource( gs_uniform ) 			u_noise_tex = {0};
_global gs_resource( gs_texture ) 			noise_tex = {0};
_global gs_resource( gs_command_buffer ) 	cb = {0};
_global gs_resource( gs_vertex_buffer ) 	vbo = {0};
_global gs_resource( gs_index_buffer ) 		ibo = {0};
_global gs_resource( gs_uniform )			u_proj = {0};
_global gs_resource( gs_uniform )			u_view = {0};
_global gs_resource( gs_uniform )			u_model = {0};
_global model_t 							terrain_model = {0};

// Forward Decls.
gs_result app_init();		// Use to init your application
gs_result app_update();		// Use to update your application
gs_result app_shutdown();	// Use to shutdown your appliaction

void render_scene();
void generate_terrain_mesh( f32* noise_data, u32 width, u32 height );

int main( int argc, char** argv )
{
	// This is our app description. It gives internal hints to our engine for various things like 
	// window size, title, as well as update, init, and shutdown functions to be run. 
	gs_application_desc app = {0};
	app.window_title 		= "Terrain Demo";
	app.window_width 		= 800;
	app.window_height 		= 600;
	app.init 				= &app_init;
	app.update 				= &app_update;
	app.shutdown 			= &app_shutdown;

	// Construct internal instance of our engine
	gs_engine* engine = gs_engine_construct( app );

	// Run the internal engine loop until completion
	gs_result res = engine->run();

	// Check result of engine after exiting loop
	if ( res != gs_result_success ) 
	{
		gs_println( "Error: Engine did not successfully finish running." );
		return -1;
	}

	gs_println( "Gunslinger exited successfully." );

	return 0;	
}

gs_result app_init()
{
	// Ripped from Sebastian Lague's demo
	const f32 scale = 100.f;
	const u32 octaves = 4;
	const f32 persistence = 0.5f; 
	const f32 lacunarity = 2.f;
	const u32 map_width = 512;
	const u32 map_height = 512;

	terrain_type regions[] = {
		{0.3f, {10, 20, 150, 255}},		// Deep Water
		{0.5f, {10, 50, 250, 255}},		// Shallow Water
		{0.53f, {255, 255, 153, 255}},	// Sand/Beach
		{0.6f, {100, 170, 40, 255}},	// Grass
		{0.65f, {100, 140, 30, 255}},	// Grass2
		{0.8f, {153, 102, 10, 255}},	// Rock
		{0.85f, {51, 26, 0, 255}},		// Rock2
		{1.0f, {200, 190, 210, 255}}	// Snow
	};

	// Maps
	f32* noise_map = gs_malloc( map_width * map_height * sizeof(f32) );
	color_t* color_map= gs_malloc( map_width * map_height * sizeof(color_t) );

	gs_assert( noise_map );
	gs_assert( color_map );

	f32 max_noise_height = f32_min;
	f32 min_noise_height = f32_max;

	for ( s32 y = 0; y < map_height; y++ ) 
	{
		for (s32 x = 0; x < map_width; x++) 
		{
			f32 amplitude = 1.f;
			f32 frequency = 1.f;
			f32 noise_height = 0.f;

			for ( u32 i = 0; i < octaves; ++i )
			{
				f32 sample_x = (x / scale) * frequency;
				f32 sample_y = (y / scale) * frequency;

				f32 p_val = noise2( sample_x, sample_y );
				noise_height += p_val * amplitude;

				amplitude *= persistence;
				frequency *= lacunarity;
			}

			noise_map[ y * map_width + x ] = noise_height;

			if ( noise_height > max_noise_height )
				max_noise_height = noise_height;
			else if ( noise_height < min_noise_height )
				min_noise_height = noise_height;
		}
	}

	// Renormalize ranges between [0.0, 1.0]
	for ( s32 y = 0; y < map_height; ++y )
	{
		for ( s32 x = 0; x < map_width; ++x )
		{
			noise_map[ y * map_width + x ] = gs_map_range( min_noise_height, max_noise_height, 
				0.0f, 1.f, noise_map[ y * map_width + x ] );
		}
	}

	u32 num_regions = sizeof(regions) / sizeof(terrain_type);

	// We'll then create a color map from these noise values
	for ( s32 y = 0; y < map_height; y++ ) 
	{
		for ( s32 x = 0; x < map_width; x++ ) 
		{
			u32 idx = y * map_width + x;
			f32 p = noise_map[ idx ];
			for ( u32 i = 0; i < num_regions; ++i ) {
				if ( p <= regions[ i ].height ) {
					color_map[ idx ] = regions[ i ].color;
					break;
				}
			}
		}
	}

	// Generate terrain mesh data from noise
	generate_terrain_mesh( noise_map, map_width, map_height );

	// Graphics api instance
	gs_graphics_i* gfx = gs_engine_instance()->ctx.graphics;
	// Platform api instance
	gs_platform_i* platform = gs_engine_instance()->ctx.platform;

	// Make our noise texture for gpu
	gs_texture_parameter_desc t_desc = gs_texture_parameter_desc_default();
	t_desc.width = map_width;
	t_desc.height = map_height;
	t_desc.mag_filter = gs_nearest;
	t_desc.min_filter = gs_nearest;
	t_desc.mipmap_filter = gs_nearest;
	t_desc.data = color_map; 

	// Construct texture
	noise_tex = gfx->construct_texture( t_desc );

	// Construct shader
	usize sz;
	char* v_src = platform->read_file_contents( "assets/shaders/terrain.v.glsl", "r", &sz );
	char* f_src = platform->read_file_contents( "assets/shaders/terrain.f.glsl", "r", &sz );
	shader = gfx->construct_shader( v_src, f_src );

	// Construct uniforms
	u_noise_tex = gfx->construct_uniform( shader, "s_noise_tex", gs_uniform_type_sampler2d );
	u_proj = gfx->construct_uniform( shader, "u_proj", gs_uniform_type_mat4 );
	u_view = gfx->construct_uniform( shader, "u_view", gs_uniform_type_mat4 );
	u_model = gfx->construct_uniform( shader, "u_model", gs_uniform_type_mat4 );

	// Construct command buffer for rendering
	cb = gfx->construct_command_buffer();

	// Vertex buffer layout information
	gs_vertex_attribute_type layout[] = {
		gs_vertex_attribute_float3,
		gs_vertex_attribute_float2	
	};
	u32 layout_count = sizeof(layout) / sizeof(gs_vertex_attribute_type);

	f32 vertices[] = 
	{
	    // positions         // texture coords
	     1.0f,  1.0f, 0.0f,  1.0f, 1.0f, // top right
	     1.0f, -1.0f, 0.0f,  1.0f, 0.0f, // bottom right
	    -1.0f, -1.0f, 0.0f,  0.0f, 0.0f, // bottom left
	    -1.0f,  1.0f, 0.0f,  0.0f, 1.0f  // top left 
	};

	// Construct vertex buffer
	vbo = gfx->construct_vertex_buffer( layout, layout_count, vertices, sizeof(vertices) );

	u32 indices[] = 
	{  
	    0, 1, 3, // first triangle
	    1, 2, 3  // second triangle
	};

	// Construct index buffer
	ibo = gfx->construct_index_buffer( indices, sizeof(indices) );

	// Free data
	gs_free( noise_map );
	gs_free( color_map );
	gs_free( v_src );
	gs_free( f_src );

	return gs_result_success;
}

typedef struct terrain_vert_data_t
{
	gs_vec3 position;
	gs_vec3 normal;
	gs_vec2 tex_coord;
} terrain_vert_data_t;

void generate_terrain_mesh( f32* noise_data, u32 width, u32 height )
{
	gs_graphics_i* gfx = gs_engine_instance()->ctx.graphics;

	gs_dyn_array( gs_vec3 ) positions = gs_dyn_array_new( gs_vec3 );
	gs_dyn_array( gs_vec3 ) normals = gs_dyn_array_new( gs_vec3 );
	gs_dyn_array( gs_vec2 ) uvs = gs_dyn_array_new( gs_vec2 );
	gs_dyn_array( u32 ) tris = gs_dyn_array_new( u32 );

	// Generate triangles, calculate normals, calculate uvs
	f32 top_left_x = (f32)(width - 1) / -2.f;
	f32 top_left_z = (f32)(height - 1) / 2.f;

	// Generate mesh data
	for ( u32 y = 0; y < height; ++y )
	{
		for ( u32 x = 0; x < width; ++x )
		{
			u32 idx = y * width + x;

			// Want to define some way of being able to pass in a curve to evaluate data for this
			f32 nd = noise_data[ idx ];
			f32 mult = gs_map_range( 0.f, 1.f, 1.f, 10.f, nd );
			gs_dyn_array_push( positions, ((gs_vec3){top_left_x + x, nd * mult, top_left_z - y }) );
			gs_dyn_array_push( uvs, ((gs_vec2){ x / (f32)width, y / (f32)height }) );

			if ( x < (width - 1) && y < height - 2 )
			{
				// Add triangle 
				gs_dyn_array_push( tris, idx );
				gs_dyn_array_push( tris, idx + width );
				gs_dyn_array_push( tris, idx + width + 1 );

				// Add triangle
				gs_dyn_array_push( tris, idx + width + 1 );
				gs_dyn_array_push( tris, idx + 1 );
				gs_dyn_array_push( tris, idx );
			}
		}
	}

	// Now that we have positions, uvs, and triangles, need to calculate normals for each triangle
	// For now, just put normal as UP, cause normals are going to take more time to do
	for ( u32 i = 0; i < width * height; ++i )
	{
		gs_dyn_array_push( normals, ((gs_vec3){ 0.f, 1.f, 0.f }) );
	}

	// Batch vertex data together
	usize vert_data_size = gs_dyn_array_size( tris ) * sizeof(terrain_vert_data_t);
	f32* vertex_data = gs_malloc( vert_data_size );

	gs_vertex_attribute_type layout[] = {
		gs_vertex_attribute_float3,
		gs_vertex_attribute_float3,
		gs_vertex_attribute_float2
	};
	u32 layout_count = sizeof(layout) / sizeof(gs_vertex_attribute_type);

	// Have to interleave data
	gs_for_range_i( gs_dyn_array_size( tris ) )
	{
		u32 base_idx = i * 8;
		u32 idx = tris[ i ];
		gs_vec3 pos = positions[ idx ];
		gs_vec3 norm = normals[ idx ];
		gs_vec2 uv = uvs[ idx ];

		vertex_data[ base_idx + 0 ] = pos.x;
		vertex_data[ base_idx + 1 ] = pos.y;
		vertex_data[ base_idx + 2 ] = pos.z;
		vertex_data[ base_idx + 3 ] = 0.f;
		vertex_data[ base_idx + 4 ] = 1.f;
		vertex_data[ base_idx + 5 ] = 0.f;
		vertex_data[ base_idx + 6 ] = uv.x;
		vertex_data[ base_idx + 7 ] = uv.y;
	}

	// Create mesh 
	terrain_model.vbo = gfx->construct_vertex_buffer( layout, layout_count, vertex_data, vert_data_size );
	terrain_model.vertex_count = gs_dyn_array_size( tris );

	// Free used memory
	gs_dyn_array_free( positions );
	gs_dyn_array_free( uvs );
	gs_dyn_array_free( normals );
	gs_dyn_array_free( tris );
	gs_free( vertex_data );
}

gs_result app_update()
{
	// Grab global instance of engine
	gs_engine* engine = gs_engine_instance();

	// If we press the escape key, exit the application
	if ( engine->ctx.platform->key_pressed( gs_keycode_esc ) )
	{
		return gs_result_success;
	}

	// Render terrain
	render_scene();

	// Otherwise, continue
	return gs_result_in_progress;
}

gs_result app_shutdown()
{
	return gs_result_success;
}

void render_scene()
{
	// Grab graphics api instance
	gs_graphics_i* gfx = gs_engine_instance()->ctx.graphics;

	// Clear screen
	f32 clear_color[4] = { 0.3f, 0.3f, 0.3f, 1.f };
	gfx->set_view_clear( cb, clear_color );

	// Set depth flags
	gfx->set_depth_enabled( cb, true );

	// Bind shader
	gfx->bind_shader( cb, shader );

	// Bind texture
	gfx->bind_texture( cb, u_noise_tex, noise_tex, 0 );	

	static f32 t = 0.f;
	t += 0.1f * gs_engine_instance()->ctx.platform->time.delta;
	gs_mat4 model = gs_mat4_identity();
	gs_vqs xform = gs_vqs_default();
	gs_quat rot = gs_quat_angle_axis( gs_deg_to_rad(20.f), (gs_vec3){1.f, 0.f, 0.f});
	rot = gs_quat_mul_quat( rot, gs_quat_angle_axis( t, (gs_vec3){0.f, 1.f, 0.f}));
	xform.rotation = rot;
	xform.scale = (gs_vec3){0.2f, 1.f, 0.2f};
	model = gs_vqs_to_mat4( &xform );
	gs_mat4 view = gs_mat4_identity();
	gs_mat4 proj = gs_mat4_identity();
	view = gs_mat4_translate((gs_vec3){-4.f, 3.f, -100.f});
	proj = gs_mat4_perspective(45.f, 800.f/600.f, 0.01f, 1000.f);

	gfx->bind_uniform( cb, u_view, &view );
	gfx->bind_uniform( cb, u_proj, &proj );
	gfx->bind_uniform( cb, u_model, &model );

	// Bind vertex buffer of terrain
	gfx->bind_vertex_buffer( cb, terrain_model.vbo );

	// Draw
	gfx->draw( cb, 0, terrain_model.vertex_count );

	// Submit command buffer to graphics api for final render
	gfx->submit_command_buffer( cb );

}

