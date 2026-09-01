# whatgever

Light maps are kind of simple. at least, my native understanding of it is.

we are building a CHART
for each face of all meshes in the scene (so not the internal point storage, the actual faces with _their vertices (meaning that vertices have unique normals per face!)) 
    - establish an arbitrary u,v coordinate on that plane. 
    - project each vertex in that plane to that coordinate system.
    - subtract uv_min from each vertex_uv to center around 0.
    - now you have all vertices expressed in their u,v from [(0,0), max_vertex_uv]
    - save this as a CHART.

now you have a list of Charts. Charts retain information like :
 - source entity
 - source face normal (faces are keyed by their normals because they are stable)
 - uv_extent
 - start_xy_in_the_atlas
 - which_atlas_page_this_face_is_packed_in
 - shrinkage_factor
 - border_in_texels

some of these are relevant to packing:
we want to pack these face uv blocks because it will be the information how much light these faces receive from any or all particular lights. during rendering, we will sample from this texture to save us from having to do advanced lighting calculation at runtime. so let's imagine a scene with 1 box and 1 point light source.

the faces can be very large, or very small, whatever. or very many. you can imagine that we cannot pack them all on a single texture, or even that we should. so usually, we pack them on a texture at some kind of resolution, a factor of the original resolution. meaning we "shrink" down all uv coordiinates of the vertices of the face by some factor so we can fit more faces on a single page. this is the shrinkage_factor. 
finally, because usually you do either halfpixel sampling or antialiasing or whatever kind of techniques, you don't want to accidentally sample lighjt information from a different face. you create a border around each face expressed in texels. Texels just means "texture pixels". but it's pixels in texture space so texels.

we pack a texture (called an "atlas") with these faces. if there's too many faces for a single texture ( a single "page"), we create a new "page".
for each face you pack, the packing algorithm gives you back a starting x,y that we store to re-retrieve our texture sampling location. it also gives us the atlas page. we store this information as like vec3i x,y,z. with x,y being the starting position and z being the page. cool! then for each of the vertices in that face, we need to set this x,y,z coordinate to sample from, so the fragments are correctly interpolated. we then sample the light information for each fragment from the lightmap. that's it!


there's way more nuance that I cannot fully comprehend yet. I understand that lighting is not linear (or the way humans perceive it) so storing rgb lighting is bad style, so people use different representations to sstore the lightmap. there's one where you do RGB9E5 where RGB9 are mantissa bits and E5 is the exponent to express a floating point range in which this is done. or you just have 10/11/11 bit floats or whatever. but you need a large range of representation.

note that that is _NOT_ HDR if you keep confusing it. they are similar in the sense that they are about dynamic range but HDR is more complicated and I don't understand.

