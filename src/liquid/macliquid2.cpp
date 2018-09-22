/*
**	macliquid2.cpp
**
**	This is part of Shiokaze, a research-oriented fluid solver for computer graphics.
**	Created by Ryoichi Ando <rand@nii.ac.jp> on April 17, 2017.
**
**	Permission is hereby granted, free of charge, to any person obtaining a copy of
**	this software and associated documentation files (the "Software"), to deal in
**	the Software without restriction, including without limitation the rights to use,
**	copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
**	Software, and to permit persons to whom the Software is furnished to do so,
**	subject to the following conditions:
**
**	The above copyright notice and this permission notice shall be included in all copies
**	or substantial portions of the Software.
**
**	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
**	INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
**	PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
**	HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
**	CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
**	OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
//
#include "macliquid2.h"
#include <shiokaze/core/filesystem.h>
#include <shiokaze/array/shared_array2.h>
#include <shiokaze/array/macarray_extrapolator2.h>
#include <shiokaze/utility/utility.h>
#include <shiokaze/core/dylibloader.h>
#include <cmath>
//
SHKZ_USING_NAMESPACE
//
macliquid2::macliquid2 () {
	//
	m_param.gravity = vec2d(0.0,-9.8);
	m_param.volume_correction = true;
	m_param.volume_change_tol_ratio = 0.03;
	//
	m_shape = shape2{64,32};
	m_dx = m_shape.dx();
}
//
void macliquid2::load( configuration &config ) {
	//
	std::string name("waterdrop2"); config.get_string("Name",name,"Scene file name");
	m_dylib.open_library(filesystem::find_libpath(name));
	m_dylib.load(config);
	m_dylib.overwrite(config);
}
//
void macliquid2::configure( configuration &config ) {
	//
	m_dylib.configure(config);
	//
	config.get_vec2d("Gravity",m_param.gravity.v,"Gravity vector");
	config.get_bool("VolumeCorrection", m_param.volume_correction,"Should perform volume correction");
	config.get_double("VolumeChangeTolRatio",m_param.volume_change_tol_ratio,"Volume change tolerance ratio");
	//
	config.get_unsigned("ResolutionX",m_shape[0],"Resolution towards X axis");
	config.get_unsigned("ResolutionY",m_shape[1],"Resolution towards Y axis");
	//
	double scale (1.0);
	config.get_double("ResolutionScale",scale,"Resolution doubling scale");
	//
	m_shape *= scale;
	m_dx = m_shape.dx();
}
//
void macliquid2::setup_window( std::string &name, int &width, int &height ) const {
	double ratio = m_shape[1] / (double)m_shape[0];
	height = width * ratio;
}
//
void macliquid2::post_initialize () {
	//
	auto initialize_func = reinterpret_cast<void(*)(const shape2 &shape, double dx)>(m_dylib.load_symbol("initialize"));
	if( initialize_func ) {
		initialize_func(m_shape,m_dx);
	}
	//
	// Initialize arrays
	m_force_exist = false;
	m_previous_injected = false;
	m_velocity.initialize(m_shape);
	m_external_force.initialize(m_shape);
	m_solid.initialize(m_shape.nodal());
	m_fluid.initialize(m_shape.cell());
	//
	// Assign initial variables from script
	m_macutility->assign_initial_variables(m_dylib,m_velocity,&m_solid,&m_fluid);
	m_velocity.set_touch_only_actives(true);
	//
	// Assign to surface tracker
	m_macsurfacetracker->assign(m_solid,m_fluid);
	m_initial_volume = m_gridutility->get_area(m_solid,m_fluid);
	//
	shared_macarray2<double> velocity_actives(m_velocity.type());
	serial::for_each( DIM2, [&]( size_t dim ) {
		velocity_actives()[dim].activate_inside_as(m_fluid);
		velocity_actives()[dim].activate_inside_as(m_fluid,vec2i(dim==0,dim==1));
	});
	m_velocity.copy_active_as(velocity_actives());
	//
	double max_u = m_macutility->compute_max_u(m_velocity);
	if( max_u ) {
		//
		// Project to make sure that the velocity field is divergence free at the beggining
		double CFL = m_timestepper->get_target_CFL();
		m_macproject->project(CFL*m_dx/max_u,m_velocity,m_solid,m_fluid);
	}
	//
}
//
void macliquid2::drag( int width, int height, double x, double y, double u, double v ) {
	//
	m_macutility->add_force(vec2d(x,y),vec2d(u,v),m_external_force);
	m_force_exist = true;
}
//
void macliquid2::inject_external_force( macarray2<double> &m_velocity, double dt ) {
	//
	if( m_force_exist ) {
		m_velocity.set_touch_only_actives(true);
		m_velocity += m_external_force;
		m_external_force.clear();
		m_force_exist = false;
	}
	// Add gravity force
	m_velocity += dt*m_param.gravity;
}
//
void macliquid2::set_volume_correction( macproject2_interface *macproject ) {
	//
	// Set volume correction if requested
	if( m_param.volume_correction ) {
		double volume = m_gridutility->get_area(m_solid,m_fluid);
		if( std::abs(1.0-volume/m_initial_volume) > m_param.volume_change_tol_ratio ) {
			double target_volume;
			if( volume > m_initial_volume ) {
				target_volume = (1.0+m_param.volume_change_tol_ratio) * m_initial_volume;
			} else {
				target_volume = (1.0-m_param.volume_change_tol_ratio) * m_initial_volume;
			}
			macproject->set_target_volume(volume,target_volume);
		}
	}
}
//
void macliquid2::extend_both() {
	//
	char bandwidth_half = m_fluid.get_levelset_halfwidth();
	auto current_CFL = std::ceil(m_timestepper->get_current_CFL());
	macarray_extrapolator2::extrapolate<double>(m_velocity,bandwidth_half+current_CFL);
	m_macutility->constrain_velocity(m_solid,m_velocity);
	m_fluid.dilate(bandwidth_half+current_CFL);
}
//
bool macliquid2::inject_liquid( double dt, array2<double> &fluid, macarray2<double> &velocity, double &amount_injected ) const {
	//
	auto inject_func = reinterpret_cast<void(*)( const vec2d &p, double time, double &fluid, vec2d &velocity )>(m_dylib.load_symbol("inject"));
	//
	bool did_set (false);
	amount_injected = 0.0;
	if( inject_func ) {
		char half_width = fluid.get_levelset_halfwidth();
		double time = m_timestepper->get_current_time();
		std::vector<bool> did_set_threads(fluid.get_thread_num(),false);
		std::vector<double> amount_injected_threads(fluid.get_thread_num(),0.0);
		fluid.parallel_all([&](int i, int j, auto &it, int tn) {
			vec2d p = m_dx*vec2i(i,j).cell(), u;
			double f (1.0);
			inject_func(p,time,f,u);
			if( std::abs(f) < 2.0*half_width*m_dx ) {
				it.set(std::min(it(),f));
				did_set_threads[tn] = true;
				if( f < 0.0 ) {
					for( int dim : DIMS2 ) for( int dir=-1; dir<=1; dir+=2 ) {
						vec2d pn = m_dx*vec2i(i+dir*(dim==0),j+dir*(dim==1)).cell(), un;
						double fn;
						inject_func(pn,time,fn,un);
						if( fn >= 0.0 ) {
							double gn = dir*(fn-f)/m_dx;
							double u_middle = 0.5*(un+u)[dim];
							double flux = dt * m_dx * u_middle * gn;
							if( flux > 0.0 ) {
								amount_injected_threads[tn] += flux;
							}
						}
					}
				}
			}
		});
		for( const auto e : did_set_threads ) if( e ) {
			did_set = true;
			break;
		}
		for( const auto e : amount_injected_threads ) {
			amount_injected += e;
		}
		if( did_set ) {
			fluid.flood_fill();
			velocity.parallel_all([&](int dim, int i, int j, auto &it, int tn) {
				vec2d p = m_dx*vec2d(i,j).face(dim);
				double f (1.0);
				vec2d u;
				inject_func(p,time,f,u);
				if( f < m_dx ) {
					it.set(u[dim]);
				}
			});
		}
	}
	return did_set;
}
//
void macliquid2::idle() {
	//
	// Compute the timestep size
	double dt = m_timestepper->advance(m_macutility->compute_max_u(m_velocity)/m_dx);
	//
	// Inject liquid
	double amount_injected;
	if(inject_liquid(dt,m_fluid,m_velocity,amount_injected)) {
		if( m_previous_injected ) {
			m_initial_volume += amount_injected;
		} else {
			m_initial_volume = m_gridutility->get_area(m_solid,m_fluid);
			m_previous_injected = true;
		}
	} else {
		m_previous_injected = false;
	}
	//
	// Extend both the velocity field and the level set
	extend_both();
	//
	// Advect surface
	m_macsurfacetracker->assign(m_solid,m_fluid);
	m_macsurfacetracker->advect(m_velocity,dt);
	m_macsurfacetracker->get(m_fluid);
	//
	// Advect velocity
	shared_macarray2<double> velocity_save(m_velocity);
	m_macadvection->advect_vector(m_velocity,velocity_save(),dt);
	//
	// Add external force
	inject_external_force(m_velocity,dt);
	//
	// Set volume correction
	set_volume_correction(m_macproject.get());
	//
	// Project
	m_macproject->project(dt,m_velocity,m_solid,m_fluid);
	//
	// Report stats
	m_macstats->dump_stats(m_solid,m_fluid,m_velocity,m_timestepper.get());
}
//
void macliquid2::draw( const graphics_engine &g, int width, int height ) const {
	//
	// Draw grid lines
	m_gridvisualizer->draw_grid(g);
	//
	// Draw surface tracker
	m_macsurfacetracker->draw(g);
	//
	// Draw projection component
	m_macproject->draw(g);
	//
	// Draw levelset
	m_gridvisualizer->draw_solid(g,m_solid);
	//
	// Draw velocity
	m_macvisualizer->draw_velocity(g,m_velocity);
}
//
extern "C" module * create_instance() {
	return new macliquid2;
}
//
extern "C" const char *license() {
	return "MIT";
}