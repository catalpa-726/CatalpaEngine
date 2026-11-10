"""Regenerate Catalpa's original Grove geometry, leaf cutout, lighting probe and author scene."""
import hashlib
import json
import math
import random
import struct
import uuid
import zlib
from pathlib import Path

DEST = Path(__file__).resolve().parents[1] / 'Projects/Grove/Content'
PROJECT = '43853bc2-001f-45ed-914b-d51c70a54a9a'
rng = random.Random(26283)


def add(a, b): return tuple(x+y for x,y in zip(a,b))
def mul(a, s): return tuple(x*s for x in a)
def sub(a, b): return add(a, mul(b, -1))
def dot(a, b): return sum(x*y for x,y in zip(a,b))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def unit(a): return mul(a, 1/math.sqrt(dot(a,a)))
def vec(x,y,z): return (x,y,z)


class Mesh:
    def __init__(self, name, material, group):
        self.name, self.material, self.group = name, material, group
        self.pos, self.norm, self.uv, self.idx = [], [], [], []

    def face(self, pts, uvs=None):
        base = len(self.pos)
        normal = unit(cross(sub(pts[1],pts[0]), sub(pts[2],pts[0])))
        self.pos.extend(pts)
        self.norm.extend([normal]*len(pts))
        self.uv.extend(uvs or [(0,0),(1,0),(1,1),(0,1)][:len(pts)])
        for i in range(1,len(pts)-1):
            self.idx.extend([base,base+i,base+i+1])

    def block(self, center, size):
        x,y,z = center
        dx,dy,dz = mul(size,.5)
        a,b,c,d = (x-dx,y-dy,z+dz),(x+dx,y-dy,z+dz),(x+dx,y+dy,z+dz),(x-dx,y+dy,z+dz)
        e,f,g,h = (x-dx,y-dy,z-dz),(x+dx,y-dy,z-dz),(x+dx,y+dy,z-dz),(x-dx,y+dy,z-dz)
        for face in [(a,b,c,d),(f,e,h,g),(d,c,g,h),(e,f,b,a),(b,f,g,c),(e,a,d,h)]:
            self.face(face)

    def limb(self, a, b, ra, rb, sides=9):
        axis = unit(sub(b,a))
        tangent = unit(cross(axis,(0,1,0) if abs(axis[1])<.98 else (1,0,0)))
        bitangent = cross(axis,tangent)
        ring = [add(mul(tangent,math.cos(i*math.tau/sides)),mul(bitangent,math.sin(i*math.tau/sides))) for i in range(sides)]
        for i,v in enumerate(ring):
            w=ring[(i+1)%sides]
            self.face([add(a,mul(v,ra)),add(a,mul(w,ra)),add(b,mul(w,rb)),add(b,mul(v,rb))])


def leaf_png():
    # Heart-shaped lamina with a central vein and paired secondary veins. Alpha is authored coverage.
    s=96
    pixels=bytearray()
    for py in range(s):
        pixels.append(0)
        for px in range(s):
            x=(px+.5)/s*2-1
            y=(py+.5)/s
            t=min(1,max(0,(y-.04)/.91))
            width=.88*math.sin(math.pi*t**1.35)**.83*(.99+.012*math.sin(75*t))
            notch=max(0, .12*(1-abs(x)/.20)) if abs(x)<.20 else 0
            inside=abs(x)<width and .04<y<.95-notch
            vein=math.exp(-abs(x)*95)+.45*math.exp(-abs((t+.42*abs(x))*7-round((t+.42*abs(x))*7))*45)
            shade=1-.18*abs(x)+.025*math.sin(80*x+13*y)
            rgb=[min(255,round(base*shade+v*min(1,vein))) for base,v in [(130,46),(177,36),(60,33)]]
            pixels.extend((*rgb,255 if inside else 0))
    def chunk(kind, data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',s,s,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(pixels,9))+chunk(b'IEND',b'')


def populate():
    materials=[]; meshes=[]
    def part(name, color, roughness, group, leaf=False):
        pbr={'baseColorFactor':[*color,1.0],'metallicFactor':0,'roughnessFactor':roughness}
        m={'name':name,'pbrMetallicRoughness':pbr}
        if leaf:
            pbr['baseColorTexture']={'index':0}
            m.update(alphaMode='MASK',alphaCutoff=.5,doubleSided=True)
        else:
            m['doubleSided']=name in ('Ivory corollas','Gold throats')
        meshes.append(Mesh(name,len(materials),group)); materials.append(m)
        return meshes[-1]
    bark=part('Fluted bark',(.145,.077,.035),.90,'Catalpa tree')
    greens=[part(name,rgb,.64,'Catalpa tree',True) for name,rgb in
            [('Young leaves',(.81,1,.64)),('Leaf surface',(.48,.79,.42)),('Deep foliage',(.32,.56,.30))]]
    petals=part('Ivory corollas',(1,.94,.78),.52,'Catalpa tree')
    gold=part('Gold throats',(.89,.53,.075),.66,'Catalpa tree')
    def leaf(target, p, direction, length, width):
        axis=unit(direction)
        side=unit(cross(axis,(0,1,0) if abs(axis[1])<.98 else (1,0,0)))
        offset=mul(side,width*.5)
        end=add(p,mul(axis,length))
        target.face([sub(p,offset),sub(end,offset),add(end,offset),add(p,offset)],[(0,1),(0,0),(1,0),(1,1)])
    def flower(p, direction):
        axis=unit(direction); a=unit(cross(axis,(0,0,1))); b=cross(axis,a)
        lip=add(p,mul(axis,.102))
        count=15; outer=[]; inner=[]
        for i in range(count):
            angle=math.tau*i/count
            lobe=.5+.5*math.cos(5*angle)
            radial=add(mul(a,math.cos(angle)),mul(b,math.sin(angle)))
            outer.append(add(add(lip,mul(radial,.051+.028*lobe)),mul(axis,.02*lobe)))
            inner.append(add(add(p,mul(axis,.033)),mul(radial,.021)))
        for i in range(count):
            j=(i+1)%count
            petals.face([inner[i],outer[i],outer[j],inner[j]])
            gold.face([p,inner[i],inner[j]])
    # Curved trunk, basal flare and scaffold limbs supporting the actual foliage.
    base=(-.80,0,.35)
    p1=add(base,(.04,1.01,-.04)); fork=add(base,(.17,2.12,-.09))
    bark.limb(base,p1,.237,.147,15); bark.limb(p1,fork,.147,.122,13)
    for i in range(9):
        a=i*math.tau/9
        bark.limb(add(base,(.56*math.cos(a),.025,.56*math.sin(a))),add(base,(0,.49,0)),.075,.134)
    for branch in range(15):
        angle=branch*2.39996
        radius=rng.uniform(1.35,2.15) if branch<11 else rng.uniform(.38,.98)
        start=add(fork,(0,rng.uniform(-.67,.30),0))
        tip=add(base,(math.cos(angle)*radius,rng.uniform(3.0,3.86)+(branch>=11)*.72,math.sin(angle)*radius))
        elbow=add(add(mul(start,.53),mul(tip,.47)),(0,-.18,0))
        bark.limb(start,elbow,.093,.057); bark.limb(elbow,tip,.056,.025,7)
        for j in range(8):
            t=j*2.39996+rng.random()
            reach=rng.uniform(.23,.70)
            twig=add(tip,(math.cos(t)*reach,rng.uniform(-.24,.44),math.sin(t)*reach))
            bark.limb(tip,twig,.022,.006,5)
            for k in range(11):
                theta=k*2.39996+t
                attach=add(twig,(rng.uniform(-.15,.15),rng.uniform(-.13,.14),rng.uniform(-.15,.15)))
                leaf(rng.choice(greens),attach,(math.cos(theta),rng.uniform(-.84,.37),math.sin(theta)),rng.uniform(.29,.49),rng.uniform(.18,.32))
            if (j+branch)%3==0:
                for k in range(6):
                    theta=k*2.39996
                    point=add(twig,(.13*math.cos(theta),.045*k,.13*math.sin(theta)))
                    flower(point,(math.cos(theta),rng.uniform(.3,1.1),math.sin(theta)))

    soil=part('Planting soil',(.094,.063,.038),.98,'Courtyard')
    soil.block((0,-.122,0),(16,.16,13))
    stones=[part('Limestone '+s,c,.85,'Courtyard') for s,c in
            [('ivory',(.54,.46,.365)),('sand',(.462,.411,.328)),('warm grey',(.394,.377,.327))]]
    for row in range(-6,6):
        for col in range(-8,8):
            x=col+(row%2)*.5; z=row*.78
            if ((x+.8)/1.42)**2+((z-.35)/1.15)**2 < 1 or z < -3.92: continue
            rng.choice(stones).block((x,-.034,z),(.982,.064,.765))
    bench=part('Honed sandstone',(.51,.43,.32),.71,'Stone bench')
    bench.block((2.48,.56,-.98),(2.31,.164,.66))
    for x in [1.72,3.24]:
        bench.block((x,.24,-.98),(.24,.48,.49))
        bench.block((x,.065,-.98),(.45,.13,.62))
    plaster=part('Warm lime plaster',(.75,.69,.58),.93,'Architecture')
    frames=part('Bronze window frames',(.105,.073,.042),.40,'Architecture')
    glass=part('Blue green glazing',(.086,.157,.160),.17,'Architecture')
    plaster.block((2.00,2.73,-4.65),(9.2,5.46,.24))
    plaster.block((-5.48,2.8,-4.65),(1.18,5.6,.24))
    glass.block((-3.73,1.94,-4.71),(2.33,3.88,.16))
    for x in [-4.94,-4.19,-3.42,-2.59]:
        frames.block((x,1.94,-4.53),(.060,3.88,.17))
    for y in [.09,2.84,3.85]:
        frames.block((-3.73,y,-4.49),(2.48,.067,.18))
    plaster.block((-3.76,4.65,-4.62),(2.52,1.66,.30))
    plaster.block((-3.76,3.92,-4.13),(3.43,.17,1.35))
    # Individual planting tufts interrupt the precise grid and soften the wall base.
    shrubs=part('Sage planting',(.40,.63,.31),.77,'Courtyard',True)
    for k in range(74):
        x=rng.uniform(-5.9,5.9); z=rng.uniform(-4.01,-3.39)
        if -5 < x < -2.5: continue
        for i in range(12):
            t=rng.uniform(0,math.tau)
            leaf(shrubs,(x,.045,z),(math.cos(t),rng.uniform(.44,1.7),math.sin(t)),rng.uniform(.22,.70),rng.uniform(.08,.19))
    return materials, meshes


def make_glb(materials, meshes):
    binary=bytearray()
    doc={'asset':{'version':'2.0','generator':'Catalpa Grove'},'scene':0,'scenes':[{'nodes':list(range(len(meshes)))}],
         'nodes':[],'meshes':[],'buffers':[],'bufferViews':[],'accessors':[],'materials':materials,
         'textures':[{'source':0}],'images':[]}
    def view(data, target=None):
        while len(binary)%4: binary.append(0)
        v={'buffer':0,'byteOffset':len(binary),'byteLength':len(data)}
        if target: v['target']=target
        binary.extend(data); doc['bufferViews'].append(v)
        return len(doc['bufferViews'])-1
    def accessor(rows, typ, ints=False, bounds=False):
        components=len(rows[0])
        fmt=struct.Struct('<'+('I' if ints else 'f')*components)
        data=b''.join(fmt.pack(*row) for row in rows)
        item={'bufferView':view(data,34963 if ints else 34962),'componentType':5125 if ints else 5126,
              'count':len(rows),'type':typ}
        if bounds:
            item['min']=[min(row[i] for row in rows) for i in range(components)]
            item['max']=[max(row[i] for row in rows) for i in range(components)]
        doc['accessors'].append(item); return len(doc['accessors'])-1
    for m in meshes:
        index=len(doc['meshes'])
        attr={'POSITION':accessor(m.pos,'VEC3',bounds=True),'NORMAL':accessor(m.norm,'VEC3'),
              'TEXCOORD_0':accessor(m.uv,'VEC2')}
        primitive={'attributes':attr,'indices':accessor([(i,) for i in m.idx],'SCALAR',ints=True),'material':m.material}
        doc['meshes'].append({'name':m.name,'primitives':[primitive]})
        doc['nodes'].append({'name':m.name,'mesh':index})
    doc['images'].append({'name':'Catalpa leaf cutout','bufferView':view(leaf_png()),'mimeType':'image/png'})
    doc['buffers']=[{'byteLength':len(binary)}]
    meta=json.dumps(doc,separators=(',',':')).encode()
    meta+=b' '*(-len(meta)%4); binary.extend(b'\0'*(-len(binary)%4))
    data=struct.pack('<II',len(meta),0x4e4f534a)+meta+struct.pack('<II',len(binary),0x004e4942)+binary
    return struct.pack('<4sII',b'glTF',2,len(data)+12)+data


def make_probe():
    # Linear sky radiance. The authored directional light supplies the sharp direct sun.
    width,height=64,32
    out=bytearray(f'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {height} +X {width}\n'.encode())
    for row in range(height):
        pixels=[]
        for col in range(width):
            lat=(.5-(row+.5)/height)*math.pi; az=(col+.5)/width*math.tau
            h=max(0,math.sin(lat))
            glow=max(0,math.cos(az-.8)*math.cos(lat)*.78+math.sin(lat)*.62)**9
            c=[(.09 if lat<0 else .74-.37*h)+glow*.94,(.09 if lat<0 else .81-.22*h)+glow*.64,
               (.075 if lat<0 else .88+.16*h)+glow*.19]
            _,exponent=math.frexp(max(c))
            pixels.append([int(v*2**(8-exponent)) for v in c]+[exponent+128])
        out.extend([2,2,0,width])
        for channel in zip(*pixels): out.extend([width,*channel])
    return out


def make_scene(meshes):
    def stable(domain): return str(uuid.UUID(bytes=hashlib.sha256(domain.encode()).digest()[:16]))
    def asset(file,key): return stable(f'{PROJECT}/content/{file}/{key}')
    nodes=[]
    def node(name, group=None, pos=(0,0,0), rotation=(1,0,0,0), **parts):
        nodes.append({'name':name,'id':stable('grove/entity/'+name),'parent':stable('grove/entity/'+group) if group else None,
                      'translation':list(pos),'rotation':list(rotation),'scale':[1,1,1],**parts})
    for group in dict.fromkeys(m.group for m in meshes): node(group)
    for i,m in enumerate(meshes):
        node(m.name,m.group,mesh={'asset':asset('Grove.glb',f'mesh/{i}/primitive/0'),
                                  'materials':[asset('Grove.glb',f'material/{m.material}')],
                                  'color':[1,1,1,1],'visible':True})
    def orient(yaw,pitch):
        cy,sy=math.cos(yaw/2),math.sin(yaw/2); cp,sp=math.cos(pitch/2),math.sin(pitch/2)
        return (cy*cp,cy*sp,sy*cp,-sy*sp)
    eye=(3.70,2.48,9.10); direction=unit(sub((0,2.10,-.25),eye))
    node('Courtyard camera',pos=eye,rotation=orient(math.atan2(-direction[0],-direction[2]),math.asin(direction[1])),
         camera={'projection':0,'fov':.79,'near':.09,'far':50,'height':10,'infinite':False,'reversed':False,
                 'enabled':True,'priority':1,'view':1,'history':1})
    node('Afternoon sun',rotation=orient(-.65,-.90),
         light={'type':0,'color':[1,.885,.72],'intensity':3.2,'range':30,'inner':.32,'outer':.64,'shadows':True,
                'bias':.002,'enabled':True})
    node('Courtyard sky',environment={'asset':asset('Courtyard.hdr','environment'),'intensity':.68,'rotation':0,
                                      'exposure':0,'background':True,'fog':False,'fog_color':[.66,.72,.76],
                                      'fog_density':0,'enabled':True})
    return {'schemaVersion':1,'name':'Grove','entities':nodes}


if __name__=='__main__':
    DEST.mkdir(parents=True,exist_ok=True)
    materials,meshes=populate()
    (DEST/'Grove.glb').write_bytes(make_glb(materials,meshes))
    (DEST/'Courtyard.hdr').write_bytes(make_probe())
    (DEST/'Grove.scene.json').write_text(json.dumps(make_scene(meshes),indent=2)+'\n',encoding='utf-8')
    print(f'Grove: {len(meshes)} meshes, {sum(len(m.idx)//3 for m in meshes)} triangles')


