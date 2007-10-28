/* -*- c++ -*-
    Copyright (C) 2006 Vladimir Shabanov <vshabanoff@gmail.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#include <osg/VertexProgram>
#include <osg/GL2Extensions>
#include <osg/CullFace>

#include <osgCal/SubMeshHardware>

using namespace osgCal;



SubMeshHardware::SubMeshHardware( Model*                 _model,
                                  const CoreModel::Mesh* _mesh )
    : coreModel( _model->getCoreModel() )
    , model( _model )
    , mesh( _mesh )
    , deformed( false )
{   
    setUseDisplayList( false );
    setSupportsDisplayList( false );
    // ^ no display lists since we create them manually
    
    setUseVertexBufferObjects( false ); // false is default

    setStateSet( mesh->staticHardwareStateSet.get() );
    // Initially we use static (not skinning) state set. It will
    // changed to skinning (in update() method when some animation
    // starts.

    create();

// its too expensive to create different stateset for each submesh
// which (stateset) differs only in two uniforms
// its better to set them in drawImplementation
//     setStateSet( new osg::StateSet( *mesh->hardwareStateSet.get(), osg::CopyOp::SHALLOW_COPY ) );
//     getStateSet()->addUniform( new osg::Uniform( osg::Uniform::FLOAT_VEC3, "translationVectors",
//                                                  30 /*OSGCAL_MAX_BONES_PER_MESH*/ ) );
//     getStateSet()->addUniform( new osg::Uniform( osg::Uniform::FLOAT_MAT3, "rotationMatrices",
//                                                  30 /*OSGCAL_MAX_BONES_PER_MESH*/ ) );

    setUserData( getStateSet() /*any referenced*/ );
    // ^ make this node not redundant and not suitable for merging for osgUtil::Optimizer
    // (with merging & flattening turned on some color artefacts arise)
    // TODO: how to completely disable FLATTEN_STATIC_TRANSFORMS on models?
    // it copies vertex buffer (which is per model) for each submesh
    // which takes too much memory
}

SubMeshHardware::~SubMeshHardware()
{
    releaseGLObjects(); // destroy our display lists
}

osg::Object*
SubMeshHardware::cloneType() const
{
    throw std::runtime_error( "cloneType() is not implemented" );
}

osg::Object*
SubMeshHardware::clone( const osg::CopyOp& ) const
{
    throw std::runtime_error( "clone() is not implemented" );
}

void
SubMeshHardware::drawImplementation( osg::RenderInfo& renderInfo ) const
{
    drawImplementation( renderInfo, getStateSet() );
}

static
const osg::Program::PerContextProgram*
getProgram( osg::State& state,
            const osg::StateSet* stateSet )
{
    const osg::Program* stateProgram =
        static_cast< const osg::Program* >
        ( stateSet->getAttribute( osg::StateAttribute::PROGRAM ) );
//        ( state.getLastAppliedAttribute( osg::StateAttribute::PROGRAM ) ); <- don't work in display lists

    if ( stateProgram == 0 )
    {
        throw std::runtime_error( "SubMeshHardware::drawImplementation(): can't get program (shader compilation failed?" );
    }

    return stateProgram->getPCP( state.getContextID() );
}

void
SubMeshHardware::drawImplementation( osg::RenderInfo&     renderInfo,
                                     const osg::StateSet* stateSet ) const
{
    osg::State& state = *renderInfo.getState();

    // -- Setup rotation/translation uniforms --
    if ( deformed )
    {
        const osg::Program::PerContextProgram* program = getProgram( state, stateSet );
        const osg::GL2Extensions* gl2extensions = osg::GL2Extensions::Get( state.getContextID(), true );

        // -- Calculate and bind rotation/translation uniforms --
        GLint rotationMatricesAttrib = program->getUniformLocation( "rotationMatrices" );
        if ( rotationMatricesAttrib < 0 )
        {
            rotationMatricesAttrib = program->getUniformLocation( "rotationMatrices[0]" );
            // Why the hell on ATI it has uniforms for each
            // elements? (nVidia has only one uniform for the whole array)
        }
    
        GLint translationVectorsAttrib = program->getUniformLocation( "translationVectors" );
        if ( translationVectorsAttrib < 0 )
        {
            translationVectorsAttrib = program->getUniformLocation( "translationVectors[0]" );
        }

        if ( rotationMatricesAttrib < 0 && translationVectorsAttrib < 0 )
        {
            throw std::runtime_error( "no rotation/translation uniforms in deformed mesh?" );
        }

        for( int boneIndex = 0; boneIndex < mesh->getBonesCount(); boneIndex++ )
        {
            int boneId = mesh->getBoneId( boneIndex );

            GLfloat         rotation[9];
            GLfloat         translation[3];

            model->getBoneRotationTranslation( boneId, rotation, translation );

            gl2extensions->glUniformMatrix3fv( rotationMatricesAttrib + boneIndex,
                                               1, GL_TRUE, rotation );
            if ( translationVectorsAttrib >= 0 )
            {
                gl2extensions->glUniform3fv( translationVectorsAttrib + boneIndex,
                                             1, translation );
            }
//             gl2extensions->glUniformMatrix3fv( rotationMatricesAttrib + boneIndex,
//                                                1, GL_FALSE,
//                                                rotationTranslationMatrices[ boneIndex ].first.ptr() );
//             gl2extensions->glUniform3fv( translationVectorsAttrib + boneIndex,
//                                          1,
//                                          rotationTranslationMatrices[ boneIndex ].second.ptr() );
        }
    
        GLfloat translation[3] = {0,0,0};
        GLfloat rotation[9] = {1,0,0, 0,1,0, 0,0,1};
        gl2extensions->glUniformMatrix3fv( rotationMatricesAttrib + 30, 1, GL_FALSE, rotation );
        if ( translationVectorsAttrib >= 0 )
        {
            gl2extensions->glUniform3fv( translationVectorsAttrib + 30, 1, translation );
        }
    }

    if ( model->getVertexVbo() )
    {
        // model's VBO is only needed for models with
        // DONT_CALCULATE_VERTEX_IN_SHADER flags
        model->getVertexVbo()->compileBuffer( state );
    }

    // -- Call or create display list --
    unsigned int contextID = renderInfo.getContextID();

    GLuint& dl = displayLists[ const_cast< osg::StateSet* >( stateSet ) ][ contextID ];

    if( dl != 0 )
    {
        glCallList( dl );
    }
    else
    {
        dl = generateDisplayList( contextID, getGLObjectSizeHint() );

        glNewList( dl, GL_COMPILE );
        innerDrawImplementation( renderInfo, stateSet );
        glEndList();

        glCallList( dl );
    }
}

void
SubMeshHardware::compileGLObjects(osg::RenderInfo& renderInfo) const
{
    compileGLObjects( renderInfo, mesh->staticHardwareStateSet.get() );

    if ( !mesh->rigid && !coreModel->getAnimationNames().empty() )
    {
        compileGLObjects( renderInfo, mesh->hardwareStateSet.get() );        
    }
}

void
SubMeshHardware::compileGLObjects(osg::RenderInfo& renderInfo,
                                  const osg::StateSet* stateSet ) const
{
    Geometry::compileGLObjects( renderInfo );
    unsigned int contextID = renderInfo.getContextID();

    GLuint& dl = displayLists[ const_cast< osg::StateSet* >( stateSet ) ][ contextID ];

    if( dl == 0 )
    {
        dl = generateDisplayList( contextID, getGLObjectSizeHint() );

        glNewList( dl, GL_COMPILE );
        innerDrawImplementation( renderInfo, stateSet );
        glEndList();
    }
}

void
SubMeshHardware::releaseGLObjects(osg::State* state) const
{
    Geometry::releaseGLObjects( state );

    for ( DisplayListsMap::iterator dls = displayLists.begin(); dls != displayLists.end(); ++dls )
    {
        if ( state )
        {
            GLuint& dl = dls->second[ state->getContextID() ];
            if( dl != 0 )
            {
                Drawable::deleteDisplayList( state->getContextID(), dl, getGLObjectSizeHint() );
                dl = 0;
            }    
        }
        else
        {
            for( size_t i = 0; i < dls->second.size(); i++ )
            {
                if ( dls->second[i] != 0 )
                {
                    Drawable::deleteDisplayList( i, dls->second[i], getGLObjectSizeHint() );
                    dls->second[i] = 0;
                }
            }            
        }
    }
}

void
SubMeshHardware::innerDrawImplementation( osg::RenderInfo&     renderInfo,
                                          const osg::StateSet* stateSet ) const
{   
    osg::State& state = *renderInfo.getState();
    const osg::Program::PerContextProgram* program = getProgram( state, stateSet );
    const osg::GL2Extensions* gl2extensions = osg::GL2Extensions::Get( state.getContextID(), true );
    
    // -- Bind our vertex buffers --
#define BIND(_type)                                                     \
    coreModel->getVbo(CoreModel::BI_##_type)->compileBuffer( state );   \
    coreModel->getVbo(CoreModel::BI_##_type)->bindBuffer( state.getContextID() )

#define UNBIND(_type)                                                   \
    coreModel->getVbo(CoreModel::BI_##_type)->unbindBuffer( state.getContextID() )

#ifdef OSG_CAL_BYTE_BUFFERS
    #define NORMAL_TYPE         GL_BYTE
    #define MATRIX_INDEX_TYPE   GL_UNSIGNED_BYTE
#else
    #define NORMAL_TYPE         GL_FLOAT
    #define MATRIX_INDEX_TYPE   GL_SHORT
#endif

    if ( program->getAttribLocation( "weight" ) > 0 )
    {
        BIND( WEIGHT );
        state.setVertexAttribPointer( program->getAttribLocation( "weight" ),
                                      4 , GL_FLOAT, false, 0,0);
    }

    if ( program->getAttribLocation( "index" ) > 0 )
    {
        BIND( MATRIX_INDEX );
        state.setVertexAttribPointer( program->getAttribLocation( "index" ),
//                                      4 , GL_INT, false, 0,0);   // dvorets - 17.5fps
//                                      4 , GL_BYTE, false, 0,0);  // dvorets - 20fps
//                                      4 , GL_FLOAT, false, 0,0); // dvorets - 53fps        
//                                      4 , GL_SHORT, false, 0,0); // dvorets - 57fps - GLSL int = 16 bit
                                        4 , MATRIX_INDEX_TYPE, false, 0,0);         
        // TODO: maybe ATI bug that Jan Ciger has happend due to unsupported GL_SHORT?
        // but conversion from float to int would be too expensive
        // when updating vertices on CPU.
    }
    
    BIND( NORMAL );
    state.setNormalPointer( NORMAL_TYPE, 0, 0 );

    if ( program->getAttribLocation( "binormal" ) > 0 )
    {
        BIND( BINORMAL );
        state.setVertexAttribPointer( program->getAttribLocation( "binormal" ),
                                      3 , NORMAL_TYPE, false, 0,0);
    }

    if ( program->getAttribLocation( "tangent" ) > 0 )
    {
        BIND( TANGENT );
        state.setVertexAttribPointer( program->getAttribLocation( "tangent" ),
                                      3 , NORMAL_TYPE, false, 0,0);
    }

    if ( mesh->hwStateDesc.diffuseMap != "" || mesh->hwStateDesc.normalsMap != ""
         || mesh->hwStateDesc.bumpMap != "" )
    {
        BIND( TEX_COORD );
        state.setTexCoordPointer( 0, 2, GL_FLOAT, 0, 0 );
    }

    if ( model->getVertexVbo() )
    {
        model->getVertexVbo()->bindBuffer( state.getContextID() );
    }
    else
    {
        BIND( VERTEX );        
    }
    state.setVertexPointer( 3, GL_FLOAT, 0, 0);

    // get mesh material to restore glColor after glDrawElements call
    const osg::Material* material = static_cast< const osg::Material* >
//        ( state.getLastAppliedAttribute( osg::StateAttribute::MATERIAL ) ); <- don't work in display lists
        ( stateSet->getAttribute( osg::StateAttribute::MATERIAL ) );

    // -- Draw our indexed triangles --
    BIND( INDEX ); 
    
#define DRAW                                                            \
    if ( sizeof(CalIndex) == 2 )                                        \
        glDrawElements( GL_TRIANGLES,                                   \
                        mesh->getIndexesCount(),                        \
                        GL_UNSIGNED_SHORT,                              \
                        ((CalIndex *)NULL) + mesh->getIndexInVbo() );   \
    else                                                                \
        glDrawElements( GL_TRIANGLES,                                   \
                        mesh->getIndexesCount(),                        \
                        GL_UNSIGNED_INT,                                \
                        ((CalIndex *)NULL) + mesh->getIndexInVbo() );   \
    if ( material ) glColor4fv( material->getDiffuse( osg::Material::FRONT ).ptr() );
    // TODO: ^ color restoring doesn't allow material overriding
    // since we get mesh material color, not last applied one (there is
    // no one when compiling display list). Maybe we can override Drawable::draw
    // to get actual last applied material.

#define glError()                                                       \
    {                                                                   \
        GLenum err = glGetError();                                      \
        while (err != GL_NO_ERROR) {                                    \
            fprintf(stderr, "glError: %s caught at %s:%u\n",            \
                    (char *)gluErrorString(err), __FILE__, __LINE__);   \
            err = glGetError();                                         \
        }                                                               \
    }

    GLint faceUniform = program->getUniformLocation( "face" );
#define SET_FACE_UNIFORM(_fu) \
    if ( faceUniform >= 0 ) gl2extensions->glUniform1f( faceUniform, _fu );

    if ( mesh->staticHardwareStateSet.get()->getRenderingHint()
         & osg::StateSet::TRANSPARENT_BIN )
    {
        glCullFace( GL_FRONT ); // first draw only back faces
        SET_FACE_UNIFORM( -1.0f );
        DRAW;
        SET_FACE_UNIFORM( 1.0f );
        glCullFace( GL_BACK ); // then draw only front faces
        DRAW;
    }
    else if ( mesh->hwStateDesc.sides == 2 && faceUniform >= 0 )
    {
        //glCullFace( GL_BACK ); // (already enabled) draw only front faces
        gl2extensions->glUniform1f( faceUniform, 1.0f );
        DRAW;
        glCullFace( GL_FRONT ); // then draw only back faces
        gl2extensions->glUniform1f( faceUniform, -1.0f );
        DRAW;
        glCullFace( GL_BACK ); // restore state
    }
    else // single sided (or undefined) -- simply draw it
    {
        SET_FACE_UNIFORM( 1.0f );
        DRAW; // simple draw for single-sided (or two-sided with gl_FrontFacing)
              // non-transparent meshes
    }
    
    //glError();

    state.disableAllVertexArrays();

    if ( model->getVertexVbo() )
    {
        model->getVertexVbo()->unbindBuffer( state.getContextID() );
    }
    else
    {
        UNBIND( VERTEX );        
    }
    UNBIND( INDEX );
}

void
SubMeshHardware::create()
{
    setVertexArray( model->getVertexBuffer() );

    osg::DrawElementsUInt* de = new osg::DrawElementsUInt( osg::PrimitiveSet::TRIANGLES,
                                                           mesh->getIndexesCount() );
    memcpy( &de->front(),
            (GLuint*)&coreModel->getIndexBuffer()->front()
            + mesh->getIndexInVbo(),
            mesh->getIndexesCount() * sizeof ( GLuint ) );
    addPrimitiveSet( de );

// draw arrays works slower for picking than draw elements.
//     setVertexIndices( const_cast< IndexBuffer* >( coreModel->getIndexBuffer() ) );
//     addPrimitiveSet( new osg::DrawArrays( osg::PrimitiveSet::TRIANGLES,
//                                           mesh->getIndexInVbo(),
//                                           mesh->getIndexesCount() ) );

    boundingBox = mesh->boundingBox;

    // create depth submesh for non-transparent meshes
    if ( (coreModel->getFlags() & CoreModel::USE_DEPTH_FIRST_MESHES)
         &&
         !(mesh->staticHardwareStateSet.get()->getRenderingHint()
           & osg::StateSet::TRANSPARENT_BIN) )
    {
        depthSubMesh = new SubMeshDepth( this ); 
    }
}

static
inline
osg::Vec3f
mul3( const osg::Matrix3& m,
      const osg::Vec3f& v )
{
    return osg::Vec3f( m(0,0)*v.x() + m(1,0)*v.y() + m(2,0)*v.z(),
                       m(0,1)*v.x() + m(1,1)*v.y() + m(2,1)*v.z(),
                       m(0,2)*v.x() + m(1,2)*v.y() + m(2,2)*v.z() );
}
namespace osg {
bool
operator == ( const osg::Matrix3& m1,
              const osg::Matrix3& m2 )
{
    for ( int i = 0; i < 9; i++ )
    {
        if ( m1.ptr()[ i ] != m2.ptr()[ i ] )
        {
            return false; 
        }
    }
    
    return true;
}
}

inline
float
square( float x )
{
    return x*x;
}

float
rtDistance( const std::vector< std::pair< osg::Matrix3, osg::Vec3f > >& v1,
            const std::vector< std::pair< osg::Matrix3, osg::Vec3f > >& v2,
            int count )
{
    float r = 0;
    for ( int i = 0; i < count; i++ )
    {
        for ( int j = 0; j < 9; j++ )
        {
            r += square( v1[i].first[j] - v2[i].first[j] );
        }
        r += ( v1[i].second - v2[i].second ).length2();
    }
    return r;
}

void
SubMeshHardware::update()
{   
    if ( mesh->rigid )
    {
        return; // no bones - no update
    }
    
    // -- Setup rotation matrices & translation vertices --
    std::vector< std::pair< osg::Matrix3, osg::Vec3f > > rotationTranslationMatrices;

    deformed = false;

    for( int boneIndex = 0; boneIndex < mesh->getBonesCount(); boneIndex++ )
    {
        int boneId = mesh->getBoneId( boneIndex );

        rotationTranslationMatrices.push_back( model->getBoneRotationTranslation( boneId ) );
        osg::Vec3 translation = model->getBoneTranslation( boneId );
        osg::Quat rotation    = model->getBoneRotation( boneId );

        if ( // cal3d reports nonzero translations for non-animated models
            // and non zero quaternions (seems like some FP round-off error). 
            // So we must check for deformations using some epsilon value.
            // Problem:
            //   * It is cal3d that must return correct values, no epsilons
            // But nevertheless we use this to reduce CPU load.
                 
            translation.length() > boundingBox.radius() * 1e-5 // usually 1e-6 .. 1e-7
            ||
            osg::Vec3d( rotation.x(),
                        rotation.y(),
                        rotation.z() ).length() > 1e-6 // usually 1e-7 .. 1e-8
            )
        {
            deformed = true;
//             std::cout << "quaternion: "
//                       << rotation.x << ' '
//                       << rotation.y << ' '
//                       << rotation.z << ' '
//                       << rotation.w << std::endl
//                       << "translation: "
//                       << translation.x << ' ' << translation.y << ' ' << translation.z
//                       << "; len = " << translation.length() << std::endl
//                       << "len / bbox.radius = " << translation.length() / boundingBox.radius()
//                       << std::endl;
        }
    }

    rotationTranslationMatrices.resize( 31 );
    rotationTranslationMatrices[ 30 ] = // last always identity (see #68)
        std::make_pair( osg::Matrix3( 1, 0, 0,
                                      0, 1, 0,
                                      0, 0, 1 ),
                        osg::Vec3( 0, 0, 0 ) );

    // -- Check for deformation state and select state set type --
//    std::cout << "deformed = " << deformed << std::endl;
    if ( deformed )
    {
        setStateSet( mesh->hardwareStateSet.get() );
    }
    else
    {
        setStateSet( mesh->staticHardwareStateSet.get() );
        // for undeformed meshes we use static state set which not
        // perform vertex, normal, binormal and tangent deformations
        // in vertex shader
    }

    // -- Update depthSubMesh --
    if ( depthSubMesh.valid() )
    {
        depthSubMesh->update( deformed );
    }

    // -- Check changes --    
    previousRotationTranslationMatrices.resize( 31 );
//     std::cout << "distance = "
//               << rtDistance( rotationTranslationMatrices,
//                              previousRotationTranslationMatrices )
//               << std::endl;
//    if ( rotationTranslationMatrices == previousRotationTranslationMatrices )
    if ( rtDistance( rotationTranslationMatrices,
                     previousRotationTranslationMatrices,
                     mesh->getBonesCount() ) < 1e-7 ) // usually 1e-8..1e-10
    {
//        std::cout << "didn't changed" << std::endl;
        return; // no changes
    }
    else
    {
        previousRotationTranslationMatrices = rotationTranslationMatrices;
    }

    // -- Scan indexes --
    boundingBox = osg::BoundingBox();
    
    VertexBuffer&               vb  = *model->getVertexBuffer();
    const VertexBuffer&         svb = *coreModel->getVertexBuffer();
    const WeightBuffer&         wb  = *coreModel->getWeightBuffer();
    const MatrixIndexBuffer&    mib = *coreModel->getMatrixIndexBuffer();    
   
    int baseIndex = mesh->hardwareMesh->baseVertexIndex;
    int vertexCount = mesh->hardwareMesh->vertexCount;
    
    osg::Vec3f*        v  = &vb.front()  + baseIndex; /* dest vector */   
    const osg::Vec3f*  sv = &svb.front() + baseIndex; /* source vector */   
    const osg::Vec4f*  w  = &wb.front()  + baseIndex; /* weights */         
    const MatrixIndexBuffer::value_type*
                       mi = &mib.front() + baseIndex; /* bone indexes */
    osg::Vec3f*        vEnd = v + vertexCount;        /* dest vector end */   
    
#define ITERATE( _f )                           \
    while ( v < vEnd )                          \
    {                                           \
        _f;                                     \
                                                \
        boundingBox.expandBy( *v );             \
        ++v;                                    \
        ++sv;                                   \
        ++w;                                    \
        ++mi;                                   \
    }

    #define x() r()
    #define y() g()
    #define z() b()
    #define w() a()
    
#define PROCESS_X( _process_y )                                         \
    /*if ( mi->x() != 30 )*/                                            \
        /* we have no zero weight vertices they all bound to 30th bone */ \
    {                                                                   \
        const osg::Matrix3& rm = rotationTranslationMatrices[mi->x()].first; \
        const osg::Vec3f&   tv = rotationTranslationMatrices[mi->x()].second; \
        *v = (mul3(rm, *sv) + tv) * w->x();                             \
                                                                        \
        _process_y;                                                     \
    }                                                                   
//     else
//     {
//         *v = *sv;
//     }

    // Strange, but multiplying each matrix on source vector works
    // faster than accumulating matrix and multiply at the end (as in
    // shader)
    // TODO: ^ retest it with non-indexed update
    //
    // And more strange, removing of  branches gives 5-10% speedup.
    // Seems that instruction prediction is really bad thing.
    //
    // And it seems that memory access (even for accumulating matix)
    // is really expensive (commenting of branches in accumulating code
    // doesn't change speed at all)

#define PROCESS_Y( _process_z )                                         \
    /*if ( w->y() )*/                                                   \
    {                                                                   \
        const osg::Matrix3& rm = rotationTranslationMatrices[mi->y()].first; \
        const osg::Vec3f&   tv = rotationTranslationMatrices[mi->y()].second; \
        *v += (mul3(rm, *sv) + tv) * w->y();                             \
                                                                        \
        _process_z;                                                     \
    }

#define PROCESS_Z( _process_w )                                         \
    /*if ( w->z() )*/                                                   \
    {                                                                   \
        const osg::Matrix3& rm = rotationTranslationMatrices[mi->z()].first; \
        const osg::Vec3f&   tv = rotationTranslationMatrices[mi->z()].second; \
        *v += (mul3(rm, *sv) + tv) * w->z();                               \
                                                                        \
        _process_w;                                                     \
    }

#define PROCESS_W()                                                     \
    /*if ( w->w() )*/                                                   \
    {                                                                   \
        const osg::Matrix3& rm = rotationTranslationMatrices[mi->w()].first; \
        const osg::Vec3f&   tv = rotationTranslationMatrices[mi->w()].second; \
        *v += (mul3(rm, *sv) + tv) * w->w();                               \
    }

#define STOP

    switch ( mesh->maxBonesInfluence )
    {
        case 1:
	  //#pragma omp parallel for -
	  // w/o      - 1.5s
	  // 1thread  - 1.7s
	  // 2threads - 2.1s => no win here
            ITERATE( PROCESS_X( STOP ) );
            break;

        case 2:
	  //#pragma omp parallel for						
            ITERATE( PROCESS_X( PROCESS_Y ( STOP ) ) );
            break;

        case 3:
	  //#pragma omp parallel for					       
            ITERATE( PROCESS_X( PROCESS_Y ( PROCESS_Z( STOP ) ) ) );
            break;

        case 4:
	  //#pragma omp parallel for						
            ITERATE( PROCESS_X( PROCESS_Y ( PROCESS_Z( PROCESS_W() ) ) ) );
            break;

        default:
            throw std::runtime_error( "maxBonesInfluence > 4 ???" );            
    }

    if ( coreModel->getFlags() & CoreModel::DONT_CALCULATE_VERTEX_IN_SHADER )
    {
        model->getVertexVbo()->dirty();
    }
    dirtyBound();

//    dirtyDisplayList(); //<- no display list for deformable mesh?
    // TODO: investigate display list stuff
}


// -- Depth submesh --

SubMeshDepth::SubMeshDepth( SubMeshHardware* hw )
    : hwMesh( hw )
{
    setUseDisplayList( false );
    setSupportsDisplayList( false );
    setUseVertexBufferObjects( false ); // false is default
    setStateSet( hwMesh->getCoreModelMesh()->staticDepthStateSet.get() );
    dirtyBound();

    setUserData( getStateSet() /*any referenced*/ );
    // ^ make this node not redundant and not suitable for merging for osgUtil::Optimizer
}

void
SubMeshDepth::drawImplementation(osg::RenderInfo& renderInfo) const
{
    hwMesh->drawImplementation( renderInfo, getStateSet() );
}

void
SubMeshDepth::compileGLObjects(osg::RenderInfo& renderInfo) const
{
    const CoreModel::Mesh* mesh = hwMesh->getCoreModelMesh();

    hwMesh->compileGLObjects( renderInfo, mesh->staticDepthStateSet.get() );

    if ( !mesh->rigid && !hwMesh->getCoreModel()->getAnimationNames().empty() )
    {
        hwMesh->compileGLObjects( renderInfo, mesh->depthStateSet.get() );
    }
}

void
SubMeshDepth::update( bool deformed )
{
    if ( deformed )
    {
        setStateSet( hwMesh->getCoreModelMesh()->depthStateSet.get() );
    }
    else
    {
        setStateSet( hwMesh->getCoreModelMesh()->staticDepthStateSet.get() );
    }

    dirtyBound();
}

osg::BoundingBox
SubMeshDepth::computeBound() const
{
    return hwMesh->computeBound();
}
