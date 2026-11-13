#!/usr/bin/env python3
"""Makes the LiveArea images (sce_sys) from a screenshot of the title screen taken on the console (docs/screenshots/title.jpg).
Usage: livearea_from_screenshot.py docs/screenshots/title.jpg port/vita/sce_sys   (needs Pillow)"""
from PIL import Image, ImageFilter, ImageDraw
import sys
src=Image.open(sys.argv[1]).convert('RGB'); out=sys.argv[2]
def q(im,path):
    if im.mode=='RGBA': im.save(path,optimize=True)   # frame1 keeps alpha
    else: im.convert('P',palette=Image.ADAPTIVE,colors=256).save(path,optimize=True)
q(src.resize((960,544)),out+'/pic0.png')
q(src.resize((840,500)),out+'/livearea/contents/bg0.png')
q(src.resize((280,158)),out+'/livearea/contents/startup.png')
logo=src.crop((215,10,740,255))            # the logo
q(logo.resize((128,128)),out+'/icon0.png') if False else None
sq=src.crop((215,5,740,250)); s=max(sq.size); canvas=Image.new('RGB',(s,s),(20,20,30)); canvas.paste(sq,(0,(s-sq.size[1])//2)); q(canvas.resize((128,128),Image.LANCZOS),out+'/icon0.png')
fr=logo.resize((414,int(414*logo.size[1]/logo.size[0]))).convert('RGBA')
m=Image.new('L',fr.size,0); ImageDraw.Draw(m).rounded_rectangle((6,6,fr.size[0]-7,fr.size[1]-7),20,fill=255); m=m.filter(ImageFilter.GaussianBlur(5)); fr.putalpha(m)
c=Image.new('RGBA',(414,189),(0,0,0,0)); c.paste(fr,(0,(189-fr.size[1])//2)); q(c,out+'/livearea/contents/frame1.png')
